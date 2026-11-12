/*
 * the drawing core of a render target: clips and layers, paints, filling and stroking polygons, bitmaps, glyph runs.
 */
#include "d2d_private.h"
#include "par/par.h"
#include <float.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

/* ------------------------------------------------------------------ the world */

float D2dUnitScaleX(D2dRt *rt) { return rt->state.units == D2D1_UNIT_MODE_PIXELS ? 1.0f : rt->dpi_x / 96.0f; }
float D2dUnitScaleY(D2dRt *rt) { return rt->state.units == D2D1_UNIT_MODE_PIXELS ? 1.0f : rt->dpi_y / 96.0f; }

void D2dWorldMatrix(D2dRt *rt, D2dMatrix *m)
{
    D2dMatrix scale = {D2dUnitScaleX(rt), 0, 0, D2dUnitScaleY(rt), 0, 0};
    D2dMatMul(m, &rt->state.transform, &scale);
}

static MimeVecRect surface_rect(const D2dSurface *s)
{
    MimeVecRect r = {0, 0, s->w, s->h};
    return r;
}

static void fail(D2dRt *rt, HRESULT hr)
{
    if (!rt->error)
        rt->error = hr;
}

/* ------------------------------------------------------------------ clipping */

void D2dClipReset(D2dRt *rt)
{
    if (rt->clip_set)
        mime_vec_mask_free(&rt->clip);
    rt->clip_set = FALSE;
}

BOOL D2dClipIsEmpty(D2dRt *rt) { return rt->clip_set && mime_vec_mask_is_empty(&rt->clip); }

static BOOL clip_bounds(D2dRt *rt, MimeVecRect *r)
{
    *r = surface_rect(rt->draw);
    if (rt->clip_set) {
        MimeVecRect b = mime_vec_mask_bounds(&rt->clip);
        if (b.left > r->left)
            r->left = b.left;
        if (b.top > r->top)
            r->top = b.top;
        if (b.right < r->right)
            r->right = b.right;
        if (b.bottom < r->bottom)
            r->bottom = b.bottom;
    }
    return r->right > r->left && r->bottom > r->top;
}

static BOOL push_clip_record(D2dRt *rt, D2dLayerRec *layer)
{
    if (!D2dArrayGrow(&rt->clips, sizeof(D2dClipRec), 1))
        return FALSE;
    D2dClipRec *rec = (D2dClipRec *)rt->clips.data + rt->clips.count++;
    memset(rec, 0, sizeof(*rec));
    mime_vec_mask_init(&rec->saved, &d2d_allocator);
    rec->saved_set = rt->clip_set;
    if (rt->clip_set)
        mime_vec_mask_copy(&rec->saved, &rt->clip);
    rec->layer = layer;
    return TRUE;
}

/* intersects the clip with polygons, in device pixels. */
static void clip_with_polys(D2dRt *rt, const MimeVecPolys *polys, int rule, int aa)
{
    MimeVecMask m;
    mime_vec_mask_init(&m, &d2d_allocator);
    MimeVecRect all = surface_rect(rt->draw);
    mime_vec_mask_set_polys(&m, polys, rule, aa, &all);
    if (!rt->clip_set) {
        mime_vec_mask_init(&rt->clip, &d2d_allocator);
        mime_vec_mask_copy(&rt->clip, &m);
        rt->clip_set = TRUE;
    } else {
        mime_vec_mask_combine(&rt->clip, &rt->clip, &m, MIME_VEC_COMBINE_INTERSECT);
    }
    mime_vec_mask_free(&m);
}

static void rect_polys(MimeVecPolys *polys, const D2D1_RECT_F *r, const D2dMatrix *m)
{
    MimeVecPoint pts[4];
    D2D1_POINT_2F c[4] = {{r->left, r->top}, {r->right, r->top}, {r->right, r->bottom}, {r->left, r->bottom}};
    for (int i = 0; i < 4; i++) {
        D2D1_POINT_2F p = D2dMatApply(m, c[i].x, c[i].y);
        pts[i].x = p.x;
        pts[i].y = p.y;
    }
    mime_vec_polys_add_sub(polys, pts, 4, true);
}

void D2dPushAxisAlignedClip(D2dRt *rt, const D2D1_RECT_F *clip, D2D1_ANTIALIAS_MODE mode)
{
    if (!rt->drawing || !clip || mode > D2D1_ANTIALIAS_MODE_ALIASED) {
        fail(rt, rt->drawing ? E_INVALIDARG : D2DERR_WRONG_STATE);
        return;
    }
    if (!push_clip_record(rt, nullptr)) {
        fail(rt, E_OUTOFMEMORY);
        return;
    }
    D2dMatrix world;
    D2dWorldMatrix(rt, &world);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    rect_polys(&polys, clip, &world);
    clip_with_polys(rt, &polys, MIME_VEC_FILL_WINDING, mode == D2D1_ANTIALIAS_MODE_ALIASED ? MIME_VEC_AA_NONE : MIME_VEC_AA_EXACT);
    mime_vec_polys_free(&polys);
}

static void pop_record(D2dRt *rt, D2dClipRec *rec)
{
    D2dClipReset(rt);
    if (rec->saved_set) {
        mime_vec_mask_init(&rt->clip, &d2d_allocator);
        mime_vec_mask_copy(&rt->clip, &rec->saved);
        rt->clip_set = TRUE;
    }
    mime_vec_mask_free(&rec->saved);
}

void D2dPopAxisAlignedClip(D2dRt *rt)
{
    if (!rt->drawing) {
        fail(rt, D2DERR_WRONG_STATE);
        return;
    }
    if (!rt->clips.count || ((D2dClipRec *)rt->clips.data)[rt->clips.count - 1].layer) {
        fail(rt, D2DERR_POP_CALL_DID_NOT_MATCH_PUSH);
        return;
    }
    D2dClipRec *rec = (D2dClipRec *)rt->clips.data + --rt->clips.count;
    pop_record(rt, rec);
}

/* ------------------------------------------------------------------ paints */

static uint32_t apply_opacity(uint32_t px, float opacity)
{
    if (opacity >= 1.0f)
        return px;
    return D2dScalePixel(px, (uint32_t)(D2dMaxF(opacity, 0.0f) * 255.0f + 0.5f));
}

void D2dPaintRelease(D2dPaint *p)
{
    if (p->held) {
        D2dImageData *d = p->held;
        D2dImageDataFree(d);
        D2dFree(d);
        p->held = nullptr;
    }
}

BOOL D2dPaintSetup(D2dRt *rt, D2dPaint *paint, ID2D1Brush *brush, const D2dMatrix *override_to_device)
{
    memset(paint, 0, sizeof(*paint));
    paint->opacity = 1.0f;
    D2dBrush *b = D2dBrushOf(brush);
    if (!b) {
        fail(rt, brush ? D2DERR_WRONG_FACTORY : E_INVALIDARG);
        return FALSE;
    }
    if (b->obj.factory != rt->obj.factory) {
        fail(rt, D2DERR_WRONG_FACTORY);
        return FALSE;
    }
    paint->brush = b;
    paint->opacity = b->opacity;
    D2dMatrix world, full;
    D2dWorldMatrix(rt, &world);
    D2dMatMul(&full, &b->transform, override_to_device ? override_to_device : &world);
    if (b->kind == D2D_BRUSH_SOLID) {
        D2D1_COLOR_F c = b->color;
        c.a *= D2dClampF(b->opacity, 0.0f, 1.0f);
        paint->is_solid = TRUE;
        paint->solid = D2dPackStraight(&c);
        paint->valid = TRUE;
        return TRUE;
    }
    if (!D2dMatInvert(&paint->inverse, &full))
        return FALSE;
    switch (b->kind) {
    case D2D_BRUSH_LINEAR:
    case D2D_BRUSH_RADIAL:
        paint->gradient = b->gradient;
        D2dGradientBuild(b->gradient);
        break;
    case D2D_BRUSH_BITMAP: {
        const D2dSurface *view = D2dBitmapView(b->bitmap);
        if (!view)
            return FALSE;
        paint->tex = view;
        paint->tex_sx = b->bitmap->dpi_x / 96.0f;
        paint->tex_sy = b->bitmap->dpi_y / 96.0f;
        paint->clamp_x1 = view->w;
        paint->clamp_y1 = view->h;
        paint->ex = b->extend_x;
        paint->ey = b->extend_y;
        paint->interp = b->interpolation;
        break;
    }
    default: {
        D2dImageData *d = D2dAllocZero(sizeof(*d));
        if (!d)
            return FALSE;
        HRESULT hr = D2dRenderImage(rt, b->image, d);
        if (FAILED(hr) || !d->surface.px) {
            D2dFree(d);
            fail(rt, FAILED(hr) ? hr : E_FAIL);
            return FALSE;
        }
        paint->held = d;
        paint->tex = &d->surface;
        float ux = rt->dpi_x / 96.0f, uy = rt->dpi_y / 96.0f;
        paint->tex_sx = ux;
        paint->tex_sy = uy;
        paint->tex_ox = (float)-d->x;
        paint->tex_oy = (float)-d->y;
        paint->clamp_x0 = 0;
        paint->clamp_y0 = 0;
        paint->clamp_x1 = d->surface.w;
        paint->clamp_y1 = d->surface.h;
        if (b->source.right > b->source.left && b->source.bottom > b->source.top) {
            int x0 = (int)D2dFloorF(b->source.left * ux) - d->x, y0 = (int)D2dFloorF(b->source.top * uy) - d->y;
            int x1 = (int)(b->source.right * ux + 0.999f) - d->x, y1 = (int)(b->source.bottom * uy + 0.999f) - d->y;
            paint->clamp_x0 = x0 < 0 ? 0 : x0;
            paint->clamp_y0 = y0 < 0 ? 0 : y0;
            paint->clamp_x1 = x1 > d->surface.w ? d->surface.w : x1;
            paint->clamp_y1 = y1 > d->surface.h ? d->surface.h : y1;
        }
        paint->ex = b->extend_x;
        paint->ey = b->extend_y;
        paint->interp = b->interpolation;
        break;
    }
    }
    paint->valid = TRUE;
    return TRUE;
}

/* a paint that samples a surface, mapped by a matrix from texels to device pixels. */
BOOL D2dPaintTexture(D2dPaint *paint, const D2dSurface *tex, const D2dMatrix *texel_to_device, float opacity,
                     D2D1_INTERPOLATION_MODE interp, D2D1_EXTEND_MODE ex, D2D1_EXTEND_MODE ey, const MimeVecRect *clamp)
{
    memset(paint, 0, sizeof(*paint));
    paint->opacity = opacity;
    if (!D2dMatInvert(&paint->inverse, texel_to_device))
        return FALSE;
    paint->tex = tex;
    paint->tex_sx = paint->tex_sy = 1.0f;
    paint->clamp_x0 = clamp ? clamp->left : 0;
    paint->clamp_y0 = clamp ? clamp->top : 0;
    paint->clamp_x1 = clamp ? clamp->right : tex->w;
    paint->clamp_y1 = clamp ? clamp->bottom : tex->h;
    paint->ex = ex;
    paint->ey = ey;
    paint->interp = interp;
    paint->valid = TRUE;
    return paint->clamp_x1 > paint->clamp_x0 && paint->clamp_y1 > paint->clamp_y0;
}

static inline int extend_index(int i, int lo, int hi, D2D1_EXTEND_MODE mode)
{
    int n = hi - lo;
    if (i >= lo && i < hi)
        return i;
    switch (mode) {
    case D2D1_EXTEND_MODE_WRAP: {
        int m = (i - lo) % n;
        if (m < 0)
            m += n;
        return lo + m;
    }
    case D2D1_EXTEND_MODE_MIRROR: {
        int m = (i - lo) % (2 * n);
        if (m < 0)
            m += 2 * n;
        return lo + (m >= n ? 2 * n - 1 - m : m);
    }
    default:
        return i < lo ? lo : hi - 1;
    }
}

static uint32_t texel(const D2dPaint *p, int i, int j)
{
    i = extend_index(i, p->clamp_x0, p->clamp_x1, p->ex);
    j = extend_index(j, p->clamp_y0, p->clamp_y1, p->ey);
    SIZE_T at = (SIZE_T)j * p->tex->w + i;
    return p->tex_index ? p->tex_palette[p->tex_index[at]] : p->tex->px[at];
}

static inline uint32_t lerp_px(uint32_t a, uint32_t b, uint32_t t)
{
    /* two channels at a time: a channel's sum stays under 16 bits, so nothing carries into its neighbour. */
    uint32_t it = 256 - t;
    uint32_t rb = (((a & 0xff00ffu) * it + (b & 0xff00ffu) * t + 0x800080u) >> 8) & 0xff00ffu;
    uint32_t ag = (((a >> 8) & 0xff00ffu) * it + ((b >> 8) & 0xff00ffu) * t + 0x800080u) & 0xff00ff00u;
    return rb | ag;
}

static uint32_t sample(const D2dPaint *p, float u, float v)
{
    if (p->interp == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR)
        return texel(p, (int)D2dFloorF(u), (int)D2dFloorF(v));
    float fu = u - 0.5f, fv = v - 0.5f;
    float iu = D2dFloorF(fu), iv = D2dFloorF(fv);
    uint32_t tu = (uint32_t)((fu - iu) * 256.0f), tv = (uint32_t)((fv - iv) * 256.0f);
    int i = (int)iu, j = (int)iv;
    uint32_t top = lerp_px(texel(p, i, j), texel(p, i + 1, j), tu);
    uint32_t bottom = lerp_px(texel(p, i, j + 1), texel(p, i + 1, j + 1), tu);
    return lerp_px(top, bottom, tv);
}

/* a row that stays on one line of the texture: the two texel rows and the vertical weight are found once. */
/* indexed is a constant at each call, so the test of it falls out of the loops. */
#define TEXEL(row, i) (indexed ? pal[((const BYTE *)(row))[i]] : ((const uint32_t *)(row))[i])

static inline __attribute__((always_inline)) void texture_row_of(const D2dPaint *p, float bx, float step, float v,
                                                                 int count, uint32_t *out, const BOOL indexed)
{
    const uint32_t *pal = p->tex_palette;
    const SIZE_T texel_size = indexed ? 1 : 4;
    const BYTE *base_px = indexed ? p->tex_index : (const BYTE *)p->tex->px;
    const D2dSurface *t = p->tex;
    BOOL nearest = p->interp == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
    float fv = nearest ? v : v - 0.5f, iv = D2dFloorF(fv);
    uint32_t tv = nearest ? 0 : (uint32_t)((fv - iv) * 256.0f);
    const BYTE *r0 = base_px + (SIZE_T)extend_index((int)iv, p->clamp_y0, p->clamp_y1, p->ey) * t->w * texel_size;
    const BYTE *r1 = tv ? base_px + (SIZE_T)extend_index((int)iv + 1, p->clamp_y0, p->clamp_y1, p->ey) * t->w * texel_size : r0;
    int i = 0;
    /*
     * a texture drawn at its own size on whole pixels: every pixel is one texel, and the sums
     * below are exact for these small numbers, so they would give the same texels.
     */
    if (step == 1.0f && p->tex_sx == 1.0f && !tv && bx > -1e6f && bx < 1e6f && p->tex_ox > -1e6f && p->tex_ox < 1e6f &&
        (float)(int)p->tex_ox == p->tex_ox) {
        float f0 = bx + p->tex_ox - 0.5f;
        int base = (int)f0;
        if ((float)base == f0) {
            const int cx0 = p->clamp_x0, cx1 = p->clamp_x1;
            int in0 = cx0 - base < 0 ? 0 : cx0 - base > count ? count : cx0 - base;
            int in1 = cx1 - base < in0 ? in0 : cx1 - base > count ? count : cx1 - base;
            for (; i < in0; i++)
                out[i] = TEXEL(r0, extend_index(base + i, cx0, cx1, p->ex));
            if (indexed) {
                for (; i < in1; i++)
                    out[i] = pal[r0[base + i]];
            } else {
                __builtin_memcpy(out + in0, r0 + (SIZE_T)(base + in0) * 4, (SIZE_T)(in1 - in0) * 4);
            }
            for (i = in1; i < count; i++)
                out[i] = TEXEL(r0, extend_index(base + i, cx0, cx1, p->ex));
            if (p->opacity < 1.0f)
                for (i = 0; i < count; i++)
                    out[i] = apply_opacity(out[i], p->opacity);
            return;
        }
    }
#if defined(__SSE2__)
    if (!nearest) {
        /* the four texels of a pixel in one register: the same sums as lerp_px, a channel to a 16 bit lane. */
        const __m128i zero = _mm_setzero_si128(), half = _mm_set1_epi16(128);
        const __m128i wv = _mm_set1_epi16((short)tv), iwv = _mm_set1_epi16((short)(256 - tv));
        const int cx0 = p->clamp_x0, cx1 = p->clamp_x1;
        for (; i < count; i++, bx += step) {
            /* the floor without the call, for a coordinate that fits an int. */
            float fu = bx * p->tex_sx + p->tex_ox - 0.5f;
            int ii = (int)fu;
            if (fu < -2e9f || fu > 2e9f)
                ii = (int)D2dFloorF(fu);
            else if ((float)ii > fu)
                ii--;
            uint32_t tu = (uint32_t)((fu - (float)ii) * 256.0f);
            int i0 = ii, i1 = ii + 1;
            if (i0 < cx0 || i1 >= cx1) {
                i0 = extend_index(ii, cx0, cx1, p->ex);
                i1 = extend_index(ii + 1, cx0, cx1, p->ex);
            }
            __m128i a = _mm_unpacklo_epi8(_mm_unpacklo_epi32(_mm_cvtsi32_si128((int)TEXEL(r0, i0)), _mm_cvtsi32_si128((int)TEXEL(r1, i0))), zero);
            __m128i b = _mm_unpacklo_epi8(_mm_unpacklo_epi32(_mm_cvtsi32_si128((int)TEXEL(r0, i1)), _mm_cvtsi32_si128((int)TEXEL(r1, i1))), zero);
            __m128i wu = _mm_set1_epi16((short)tu), iwu = _mm_set1_epi16((short)(256 - tu));
            __m128i h = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(a, iwu), _mm_mullo_epi16(b, wu)), half), 8);
            __m128i bottom = _mm_srli_si128(h, 8);
            __m128i o = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(h, iwv), _mm_mullo_epi16(bottom, wv)), half), 8);
            out[i] = apply_opacity((uint32_t)_mm_cvtsi128_si32(_mm_packus_epi16(o, o)), p->opacity);
        }
        return;
    }
#endif
    for (; i < count; i++, bx += step) {
        float u = bx * p->tex_sx + p->tex_ox;
        float fu = nearest ? u : u - 0.5f, iu = D2dFloorF(fu);
        uint32_t tu = nearest ? 0 : (uint32_t)((fu - iu) * 256.0f);
        int i0 = extend_index((int)iu, p->clamp_x0, p->clamp_x1, p->ex);
        uint32_t px = TEXEL(r0, i0);
        if (tu) {
            int i1 = extend_index((int)iu + 1, p->clamp_x0, p->clamp_x1, p->ex);
            px = lerp_px(px, TEXEL(r0, i1), tu);
            if (tv)
                px = lerp_px(px, lerp_px(TEXEL(r1, i0), TEXEL(r1, i1), tu), tv);
        } else if (tv) {
            px = lerp_px(px, TEXEL(r1, i0), tv);
        }
        out[i] = apply_opacity(px, p->opacity);
    }
}

#undef TEXEL

static void texture_row(const D2dPaint *p, float bx, float step, float v, int count, uint32_t *out)
{
    if (p->tex_index)
        texture_row_of(p, bx, step, v, count, out, TRUE);
    else
        texture_row_of(p, bx, step, v, count, out, FALSE);
}

void D2dPaintRow(D2dRt *rt, D2dPaint *p, int x, int y, int count, uint32_t *out)
{
    if (p->is_solid) {
        uint32_t px = D2dPremulPixel(p->solid);
        for (int i = 0; i < count; i++)
            out[i] = px;
        return;
    }
    const D2dMatrix *m = &p->inverse;
    float bx = (x + 0.5f) * m->_11 + (y + 0.5f) * m->_21 + m->_31;
    float by = (x + 0.5f) * m->_12 + (y + 0.5f) * m->_22 + m->_32;
    const D2dBrush *b = p->brush;
    if (p->tex && m->_12 == 0.0f) {
        texture_row(p, bx, m->_11, by * p->tex_sy + p->tex_oy, count, out);
        return;
    }
    for (int i = 0; i < count; i++, bx += m->_11, by += m->_12) {
        uint32_t px;
        if (p->tex) {
            px = sample(p, bx * p->tex_sx + p->tex_ox, by * p->tex_sy + p->tex_oy);
        } else {
            float t;
            if (b->kind == D2D_BRUSH_LINEAR) {
                float dx = b->p1.x - b->p0.x, dy = b->p1.y - b->p0.y;
                float len2 = dx * dx + dy * dy;
                t = len2 > 0 ? ((bx - b->p0.x) * dx + (by - b->p0.y) * dy) / len2 : 0.0f;
            } else {
                float rx = b->rx > 0 ? b->rx : 1e-6f, ry = b->ry > 0 ? b->ry : 1e-6f;
                float u = (bx - b->p0.x) / rx, v = (by - b->p0.y) / ry;
                float ox = b->origin.x / rx, oy = b->origin.y / ry;
                float dx = u - ox, dy = v - oy;
                float a = dx * dx + dy * dy;
                if (a < 1e-12f) {
                    t = 0.0f;
                } else {
                    float od = ox * dx + oy * dy, oo = ox * ox + oy * oy;
                    float disc = od * od - a * (oo - 1.0f);
                    float k = (-od + D2dSqrtF(D2dMaxF(disc, 0.0f))) / a;
                    t = k > 1e-9f ? 1.0f / k : 1e9f;
                }
            }
            D2D1_EXTEND_MODE em = p->gradient->extend;
            if (t < 0.0f || t > 1.0f) {
                if (em == D2D1_EXTEND_MODE_WRAP) {
                    t = t - D2dFloorF(t);
                } else if (em == D2D1_EXTEND_MODE_MIRROR) {
                    float f = t - 2.0f * D2dFloorF(t * 0.5f);
                    t = f > 1.0f ? 2.0f - f : f;
                } else {
                    t = t < 0.0f ? 0.0f : 1.0f;
                }
            }
            px = p->gradient->lut[(int)(t * 1023.0f + 0.5f)];
        }
        out[i] = apply_opacity(px, p->opacity);
    }
}

/* ------------------------------------------------------------------ blending spans */

static uint32_t channel_blend(D2D1_PRIMITIVE_BLEND mode, uint32_t d, uint32_t s)
{
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t a = (d >> sh) & 255, b = (s >> sh) & 255, r;
        switch (mode) {
        case D2D1_PRIMITIVE_BLEND_MIN:
            r = a < b ? a : b;
            break;
        case D2D1_PRIMITIVE_BLEND_MAX:
            r = a > b ? a : b;
            break;
        default:
            r = a + b > 255 ? 255 : a + b;
            break;
        }
        out |= r << sh;
    }
    return out;
}

static void blend_span(D2dRt *rt, uint32_t *dst, const uint32_t *src, const uint8_t *cov, int count)
{
    D2D1_PRIMITIVE_BLEND mode = rt->state.blend;
    if (mode == D2D1_PRIMITIVE_BLEND_SOURCE_OVER) {
        mime_vec_blend_row(dst, src, cov, (uint32_t)count, 255, MIME_VEC_BLEND_OVER, MIME_VEC_FMT_PARGB, MIME_VEC_FMT_PARGB);
    } else if (mode == D2D1_PRIMITIVE_BLEND_COPY) {
        mime_vec_blend_row(dst, src, cov, (uint32_t)count, 255, MIME_VEC_BLEND_COPY, MIME_VEC_FMT_PARGB, MIME_VEC_FMT_PARGB);
    } else {
        for (int i = 0; i < count; i++) {
            uint32_t c = cov ? cov[i] : 255;
            if (!c)
                continue;
            uint32_t r = channel_blend(mode, dst[i], src[i]);
            dst[i] = c == 255 ? r : lerp_px(dst[i], r, (c * 256 + 127) / 255);
        }
    }
}

typedef struct FillCtx {
    D2dRt *rt;
    D2dPaint *paint;
    uint32_t *row;
    uint8_t cov[4096];
} FillCtx;

static void fill_span(void *user, int y, int x, int count, const uint8_t *coverage)
{
    FillCtx *c = user;
    D2dRt *rt = c->rt;
    uint32_t *dst = D2dSurfaceRow(rt->draw, y) + x;
    while (count > 0) {
        int n = count > 4096 ? 4096 : count;
        memcpy(c->cov, coverage, (SIZE_T)n);
        if (rt->clip_set)
            mime_vec_mask_apply_row(&rt->clip, y, x, n, c->cov);
        if (c->paint->is_solid && rt->state.blend == D2D1_PRIMITIVE_BLEND_SOURCE_OVER) {
            mime_vec_blend_color(dst, c->paint->solid, c->cov, (uint32_t)n, MIME_VEC_BLEND_OVER, MIME_VEC_FMT_PARGB);
        } else {
            D2dPaintRow(rt, c->paint, x, y, n, c->row);
            blend_span(rt, dst, c->row, c->cov, n);
        }
        dst += n;
        coverage += n;
        x += n;
        count -= n;
    }
}

typedef struct FillJob {
    D2dRt *rt;
    D2dPaint *paint;
    const MimeVecPolys *polys;
    MimeVecRect area;
    int rule, aa;
    BOOL failed;
} FillJob;

static void fill_rows(void *user, int first, int end)
{
    FillJob *job = user;
    /* on the stack, as the heap lock would be fought over by every piece. */
    uint32_t row[4096];
    FillCtx ctx;
    ctx.rt = job->rt;
    ctx.paint = job->paint;
    ctx.row = row;
    if (mime_vec_fill_rows(&d2d_allocator, job->polys, job->rule, job->aa, &job->area, first, end, fill_span, &ctx) ==
        MIME_VEC_NOMEM)
        job->failed = TRUE;
}

void D2dFillPolys(D2dRt *rt, const MimeVecPolys *polys, BOOL winding, BOOL aliased, D2dPaint *paint)
{
    MimeVecBounds b;
    if (!mime_vec_polys_bounds(polys, &b))
        return;
    MimeVecRect area = mime_vec_bounds_to_rect(&b), cb;
    if (!clip_bounds(rt, &cb))
        return;
    if (area.left < cb.left)
        area.left = cb.left;
    if (area.top < cb.top)
        area.top = cb.top;
    if (area.right > cb.right)
        area.right = cb.right;
    if (area.bottom > cb.bottom)
        area.bottom = cb.bottom;
    if (area.right <= area.left || area.bottom <= area.top)
        return;
    FillJob job = {rt, paint, polys, area, winding ? MIME_VEC_FILL_WINDING : MIME_VEC_FILL_ALTERNATE,
                   aliased ? MIME_VEC_AA_NONE : MIME_VEC_AA_EXACT, FALSE};
    int w = area.right - area.left, h = area.bottom - area.top;
    int grain = h;
    /* every piece walks all the edges again, so shapes of many points stay whole. */
    /* a piece costs little while the other processors are awake, so a sampled fill splits much sooner. */
    BOOL ready = !paint->is_solid && mime_par_ready();
    if (h >= (ready ? 4 : 16) && (int64_t)w * h >= (ready ? 4096 : 32768) && (int64_t)polys->npts * 64 < (int64_t)w * h) {
        grain = (ready ? 2048 : 16384) / w;
        if (grain < (ready ? 2 : 4))
            grain = ready ? 2 : 4;
        if (polys->npts > 4 && grain < h / 8)
            grain = h / 8;
    }
    mime_par_rows(area.top, area.bottom, grain, fill_rows, &job);
    if (job.failed)
        fail(rt, E_OUTOFMEMORY);
}

/* ------------------------------------------------------------------ shapes */

static BOOL aliased_mode(D2dRt *rt) { return rt->state.aa == D2D1_ANTIALIAS_MODE_ALIASED; }

static BOOL check_drawing(D2dRt *rt)
{
    if (!rt->drawing) {
        fail(rt, D2DERR_WRONG_STATE);
        return FALSE;
    }
    return !D2dClipIsEmpty(rt);
}

void D2dFillRect(D2dRt *rt, const D2D1_RECT_F *rect, ID2D1Brush *brush)
{
    if (!check_drawing(rt))
        return;
    D2dPaint paint;
    if (!D2dPaintSetup(rt, &paint, brush, nullptr))
        return;
    D2dMatrix world;
    D2dWorldMatrix(rt, &world);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    rect_polys(&polys, rect, &world);
    D2dFillPolys(rt, &polys, TRUE, aliased_mode(rt), &paint);
    mime_vec_polys_free(&polys);
    D2dPaintRelease(&paint);
}

void D2dFillGeometry(D2dRt *rt, ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacity_brush)
{
    if (!check_drawing(rt))
        return;
    D2dGeometry *g = D2dGeometryOf(geometry);
    if (!g || g->obj.factory != rt->obj.factory) {
        fail(rt, g ? D2DERR_WRONG_FACTORY : E_INVALIDARG);
        return;
    }
    D2dPaint paint;
    if (!D2dPaintSetup(rt, &paint, brush, nullptr))
        return;
    D2dMatrix world;
    D2dWorldMatrix(rt, &world);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    BOOL winding;
    HRESULT hr = D2dGeometryFlatten(g, &world, D2D1_DEFAULT_FLATTENING_TOLERANCE, TRUE, &polys, &winding);
    if (FAILED(hr)) {
        fail(rt, hr);
    } else if (opacity_brush) {
        /* the geometry is drawn into a layer of its own whose alpha the opacity brush then scales. */
        D2dSurface layer;
        if (D2dSurfaceInit(&layer, rt->draw->w, rt->draw->h)) {
            D2dSurface *saved = rt->draw;
            rt->draw = &layer;
            D2dFillPolys(rt, &polys, winding, aliased_mode(rt), &paint);
            D2dPaint mask;
            if (D2dPaintSetup(rt, &mask, opacity_brush, nullptr)) {
                uint32_t *row = D2dAlloc((SIZE_T)layer.w * 4);
                if (row) {
                    for (int y = 0; y < layer.h; y++) {
                        uint32_t *px = D2dSurfaceRow(&layer, y);
                        D2dPaintRow(rt, &mask, 0, y, layer.w, row);
                        for (int x = 0; x < layer.w; x++)
                            px[x] = D2dScalePixel(px[x], row[x] >> 24);
                    }
                    D2dFree(row);
                }
                D2dPaintRelease(&mask);
            }
            rt->draw = saved;
            D2dBlendSurface(rt, &layer, 0, 0, 1.0f);
            D2dSurfaceFree(&layer);
        }
    } else {
        D2dFillPolys(rt, &polys, winding, aliased_mode(rt), &paint);
    }
    mime_vec_polys_free(&polys);
    D2dPaintRelease(&paint);
}

void D2dDrawGeometry(D2dRt *rt, ID2D1Geometry *geometry, ID2D1Brush *brush, float width, ID2D1StrokeStyle *style)
{
    if (!check_drawing(rt))
        return;
    D2dGeometry *g = D2dGeometryOf(geometry);
    if (!g || g->obj.factory != rt->obj.factory) {
        fail(rt, g ? D2DERR_WRONG_FACTORY : E_INVALIDARG);
        return;
    }
    if (!(width >= 0.0f)) {
        fail(rt, E_INVALIDARG);
        return;
    }
    D2dPaint paint;
    if (!D2dPaintSetup(rt, &paint, brush, nullptr))
        return;
    D2dMatrix world;
    D2dWorldMatrix(rt, &world);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    HRESULT hr = D2dStrokeGeometry(g, width, style, &world, D2D1_DEFAULT_FLATTENING_TOLERANCE, &polys);
    if (FAILED(hr))
        fail(rt, hr);
    else
        D2dFillPolys(rt, &polys, TRUE, aliased_mode(rt), &paint);
    mime_vec_polys_free(&polys);
    D2dPaintRelease(&paint);
}

/* ------------------------------------------------------------------ surfaces and bitmaps */

void D2dBlendSurface(D2dRt *rt, const D2dSurface *src, int dx, int dy, float opacity)
{
    MimeVecRect cb;
    if (!clip_bounds(rt, &cb))
        return;
    uint8_t *cov = D2dAlloc((SIZE_T)(src->w > 0 ? src->w : 1));
    if (!cov)
        return;
    uint32_t alpha = (uint32_t)(D2dClampF(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    for (int y = 0; y < src->h; y++) {
        int ty = y + dy;
        if (ty < cb.top || ty >= cb.bottom)
            continue;
        int x0 = dx < cb.left ? cb.left - dx : 0;
        int x1 = dx + src->w > cb.right ? cb.right - dx : src->w;
        if (x1 <= x0)
            continue;
        int n = x1 - x0;
        for (int i = 0; i < n; i++)
            cov[i] = (uint8_t)alpha;
        if (rt->clip_set)
            mime_vec_mask_apply_row(&rt->clip, ty, dx + x0, n, cov);
        blend_span(rt, D2dSurfaceRow(rt->draw, ty) + dx + x0, D2dSurfaceRow(src, y) + x0, cov, n);
    }
    D2dFree(cov);
}

/* fills polygons with a texture. */
static void draw_texture(D2dRt *rt, const D2dSurface *tex, const BYTE *index, const uint32_t *palette,
                         const D2dMatrix *texel_to_device, const MimeVecPolys *area, float opacity,
                         D2D1_INTERPOLATION_MODE interp, const MimeVecRect *clamp)
{
    if (!check_drawing(rt))
        return;
    D2dPaint paint;
    if (!D2dPaintTexture(&paint, tex, texel_to_device, opacity, interp, D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, clamp))
        return;
    paint.tex_index = index;
    paint.tex_palette = palette;
    D2dFillPolys(rt, area, TRUE, aliased_mode(rt), &paint);
}

void D2dDrawTexture(D2dRt *rt, const D2dSurface *tex, const D2dMatrix *texel_to_device, const MimeVecPolys *area,
                    float opacity, D2D1_INTERPOLATION_MODE interp, const MimeVecRect *clamp)
{
    draw_texture(rt, tex, nullptr, nullptr, texel_to_device, area, opacity, interp, clamp);
}

void D2dDrawBitmapRect(D2dRt *rt, D2dBitmap *bitmap, const D2D1_RECT_F *dest_in, float opacity,
                       D2D1_INTERPOLATION_MODE mode, const D2D1_RECT_F *source_in, const D2dMatrix *extra)
{
    if (!check_drawing(rt))
        return;
    if (bitmap->obj.factory != rt->obj.factory) {
        fail(rt, D2DERR_WRONG_FACTORY);
        return;
    }
    /* a bitmap that is still indices is drawn from them. */
    D2dSurface shape = {(int)bitmap->width, (int)bitmap->height, nullptr};
    const BYTE *index = bitmap->native ? nullptr : bitmap->index;
    const D2dSurface *view = index ? &shape : D2dBitmapView(bitmap);
    if (!view)
        return;
    float bw = bitmap->width * 96.0f / bitmap->dpi_x, bh = bitmap->height * 96.0f / bitmap->dpi_y;
    D2D1_RECT_F src = source_in ? *source_in : (D2D1_RECT_F){0, 0, bw, bh};
    D2D1_RECT_F dst = dest_in ? *dest_in : (D2D1_RECT_F){0, 0, src.right - src.left, src.bottom - src.top};
    float sw = src.right - src.left, sh = src.bottom - src.top;
    if (!(sw > 0) || !(sh > 0))
        return;
    float ax = (dst.right - dst.left) / sw, ay = (dst.bottom - dst.top) / sh;
    /* texel to user: texels are dips scaled by the bitmap dpi, then the source rectangle to the destination. */
    float sx = bitmap->dpi_x / 96.0f, sy = bitmap->dpi_y / 96.0f;
    D2dMatrix texel_to_user = {ax / sx, 0, 0, ay / sy, dst.left - src.left * ax, dst.top - src.top * ay};
    D2dMatrix world, full;
    D2dWorldMatrix(rt, &world);
    if (extra) {
        D2dMatMul(&full, &texel_to_user, extra);
        D2dMatMul(&full, &full, &world);
    } else {
        D2dMatMul(&full, &texel_to_user, &world);
    }
    MimeVecRect clamp = {(int)D2dFloorF(src.left * sx), (int)D2dFloorF(src.top * sy), (int)(src.right * sx + 0.999f),
                         (int)(src.bottom * sy + 0.999f)};
    if (clamp.left < 0)
        clamp.left = 0;
    if (clamp.top < 0)
        clamp.top = 0;
    if (clamp.right > view->w)
        clamp.right = view->w;
    if (clamp.bottom > view->h)
        clamp.bottom = view->h;
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &d2d_allocator);
    D2dMatrix outer = world;
    if (extra)
        D2dMatMul(&outer, extra, &world);
    rect_polys(&polys, &dst, &outer);
    draw_texture(rt, view, index, bitmap->palette, &full, &polys, opacity, mode, &clamp);
    mime_vec_polys_free(&polys);
}

void D2dFillOpacityMask(D2dRt *rt, D2dBitmap *mask, ID2D1Brush *brush, const D2D1_RECT_F *dest, const D2D1_RECT_F *source)
{
    if (!check_drawing(rt))
        return;
    D2dPaint paint;
    if (!D2dPaintSetup(rt, &paint, brush, nullptr))
        return;
    const D2dSurface *view = D2dBitmapView(mask);
    if (!view) {
        D2dPaintRelease(&paint);
        return;
    }
    float bw = mask->width * 96.0f / mask->dpi_x, bh = mask->height * 96.0f / mask->dpi_y;
    D2D1_RECT_F src = source ? *source : (D2D1_RECT_F){0, 0, bw, bh};
    D2D1_RECT_F dst = dest ? *dest : (D2D1_RECT_F){0, 0, src.right - src.left, src.bottom - src.top};
    float sw = src.right - src.left, sh = src.bottom - src.top;
    if (!(sw > 0) || !(sh > 0)) {
        D2dPaintRelease(&paint);
        return;
    }
    float ax = (dst.right - dst.left) / sw, ay = (dst.bottom - dst.top) / sh;
    float sx = mask->dpi_x / 96.0f, sy = mask->dpi_y / 96.0f;
    D2dMatrix texel_to_user = {ax / sx, 0, 0, ay / sy, dst.left - src.left * ax, dst.top - src.top * ay};
    D2dMatrix world, full;
    D2dWorldMatrix(rt, &world);
    D2dMatMul(&full, &texel_to_user, &world);
    D2dPaint maskp;
    MimeVecRect clamp = {(int)D2dFloorF(src.left * sx), (int)D2dFloorF(src.top * sy), (int)(src.right * sx + 0.999f),
                         (int)(src.bottom * sy + 0.999f)};
    if (clamp.right > view->w)
        clamp.right = view->w;
    if (clamp.bottom > view->h)
        clamp.bottom = view->h;
    if (clamp.left < 0)
        clamp.left = 0;
    if (clamp.top < 0)
        clamp.top = 0;
    if (D2dPaintTexture(&maskp, view, &full, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, &clamp)) {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &d2d_allocator);
        rect_polys(&polys, &dst, &world);
        MimeVecBounds b;
        MimeVecRect cb;
        if (mime_vec_polys_bounds(&polys, &b) && clip_bounds(rt, &cb)) {
            MimeVecRect area = mime_vec_bounds_to_rect(&b);
            if (area.left < cb.left) area.left = cb.left;
            if (area.top < cb.top) area.top = cb.top;
            if (area.right > cb.right) area.right = cb.right;
            if (area.bottom > cb.bottom) area.bottom = cb.bottom;
            int w = area.right - area.left;
            uint32_t *mrow = D2dAlloc((SIZE_T)(w > 0 ? w : 1) * 8);
            uint8_t *cov = D2dAlloc((SIZE_T)(w > 0 ? w : 1));
            if (mrow && cov && w > 0) {
                uint32_t *prow = mrow + w;
                for (int y = area.top; y < area.bottom; y++) {
                    D2dPaintRow(rt, &maskp, area.left, y, w, mrow);
                    D2dPaintRow(rt, &paint, area.left, y, w, prow);
                    for (int i = 0; i < w; i++)
                        cov[i] = (uint8_t)(mrow[i] >> 24);
                    if (rt->clip_set)
                        mime_vec_mask_apply_row(&rt->clip, y, area.left, w, cov);
                    /* the edges of the destination rectangle, by the scan converter's coverage of that row. */
                    blend_span(rt, D2dSurfaceRow(rt->draw, y) + area.left, prow, cov, w);
                }
            }
            D2dFree(mrow);
            D2dFree(cov);
        }
        mime_vec_polys_free(&polys);
    }
    D2dPaintRelease(&paint);
}

/* ------------------------------------------------------------------ layers */

void D2dPushLayer(D2dRt *rt, const D2D1_LAYER_PARAMETERS1 *params)
{
    if (!rt->drawing) {
        fail(rt, D2DERR_WRONG_STATE);
        return;
    }
    D2dLayerRec *layer = D2dAllocZero(sizeof(*layer));
    if (!layer || !push_clip_record(rt, layer) || !D2dSurfaceInit(&layer->surface, rt->draw->w, rt->draw->h)) {
        if (layer && rt->clips.count && ((D2dClipRec *)rt->clips.data)[rt->clips.count - 1].layer == layer)
            rt->clips.count--;
        D2dFree(layer);
        fail(rt, E_OUTOFMEMORY);
        return;
    }
    D2dMatrix world;
    D2dWorldMatrix(rt, &world);
    layer->parent = rt->draw;
    layer->target = D2dRtTargetKey(rt);
    layer->opacity = params->opacity;
    layer->brush = params->opacityBrush;
    D2D_ADDREF(layer->brush);
    layer->brush_matrix = world;
    layer->ignore_alpha = (params->layerOptions & D2D1_LAYER_OPTIONS1_IGNORE_ALPHA) != 0;
    layer->bounds = surface_rect(rt->draw);
    mime_vec_mask_init(&layer->mask, &d2d_allocator);
    mime_vec_mask_set_rect(&layer->mask, &layer->bounds);
    if (params->layerOptions & D2D1_LAYER_OPTIONS1_INITIALIZE_FROM_BACKGROUND)
        memcpy(layer->surface.px, rt->draw->px, (SIZE_T)rt->draw->w * rt->draw->h * 4);
    /* content bounds and the geometric mask shrink the mask the layer is composed with. */
    D2D1_RECT_F cbnd = params->contentBounds;
    if (cbnd.right > cbnd.left && cbnd.bottom > cbnd.top && cbnd.left > -1e30f) {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &d2d_allocator);
        rect_polys(&polys, &cbnd, &world);
        MimeVecMask m;
        mime_vec_mask_init(&m, &d2d_allocator);
        mime_vec_mask_set_polys(&m, &polys, MIME_VEC_FILL_WINDING, MIME_VEC_AA_EXACT, &layer->bounds);
        mime_vec_mask_combine(&layer->mask, &layer->mask, &m, MIME_VEC_COMBINE_INTERSECT);
        mime_vec_mask_free(&m);
        mime_vec_polys_free(&polys);
    } else if (cbnd.right <= cbnd.left && cbnd.left < 1e30f && cbnd.left > -1e30f) {
        mime_vec_mask_clear(&layer->mask);
    }
    D2dGeometry *g = D2dGeometryOf(params->geometricMask);
    if (g) {
        D2dMatrix mm;
        D2dMatMul(&mm, &params->maskTransform, &world);
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &d2d_allocator);
        BOOL winding;
        if (SUCCEEDED(D2dGeometryFlatten(g, &mm, D2D1_DEFAULT_FLATTENING_TOLERANCE, TRUE, &polys, &winding))) {
            MimeVecMask m;
            mime_vec_mask_init(&m, &d2d_allocator);
            mime_vec_mask_set_polys(&m, &polys, winding ? MIME_VEC_FILL_WINDING : MIME_VEC_FILL_ALTERNATE,
                                    params->maskAntialiasMode == D2D1_ANTIALIAS_MODE_ALIASED ? MIME_VEC_AA_NONE : MIME_VEC_AA_EXACT,
                                    &layer->bounds);
            mime_vec_mask_combine(&layer->mask, &layer->mask, &m, MIME_VEC_COMBINE_INTERSECT);
            mime_vec_mask_free(&m);
        }
        mime_vec_polys_free(&polys);
    }
    rt->draw = &layer->surface;
}

void D2dPopLayer(D2dRt *rt)
{
    if (!rt->drawing) {
        fail(rt, D2DERR_WRONG_STATE);
        return;
    }
    if (!rt->clips.count || !((D2dClipRec *)rt->clips.data)[rt->clips.count - 1].layer) {
        fail(rt, D2DERR_POP_CALL_DID_NOT_MATCH_PUSH);
        return;
    }
    D2dClipRec *rec = (D2dClipRec *)rt->clips.data + --rt->clips.count;
    D2dLayerRec *layer = rec->layer;
    /* a layer popped after its device context changed target is composed onto the target it began on. */
    D2dSurface *current = rt->draw;
    BOOL elsewhere = layer->target != D2dRtTargetKey(rt);
    rt->draw = layer->parent;
    pop_record(rt, rec);
    /* the layer's own pixels are composed with the mask, the opacity and the opacity brush. */
    D2dSurface *src = &layer->surface;
    MimeVecRect cb;
    if (clip_bounds(rt, &cb)) {
        MimeVecRect mb = mime_vec_mask_bounds(&layer->mask);
        if (mb.left > cb.left) cb.left = mb.left;
        if (mb.top > cb.top) cb.top = mb.top;
        if (mb.right < cb.right) cb.right = mb.right;
        if (mb.bottom < cb.bottom) cb.bottom = mb.bottom;
        D2dPaint brush;
        BOOL have_brush = layer->brush && D2dPaintSetup(rt, &brush, layer->brush, &layer->brush_matrix);
        int w = cb.right - cb.left;
        uint8_t *cov = D2dAlloc((SIZE_T)(w > 0 ? w : 1));
        uint32_t *brow = have_brush && w > 0 ? D2dAlloc((SIZE_T)w * 4) : nullptr;
        uint32_t opacity = (uint32_t)(D2dClampF(layer->opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
        if (cov && w > 0) {
            for (int y = cb.top; y < cb.bottom; y++) {
                for (int i = 0; i < w; i++)
                    cov[i] = 255;
                mime_vec_mask_apply_row(&layer->mask, y, cb.left, w, cov);
                if (brow) {
                    D2dPaintRow(rt, &brush, cb.left, y, w, brow);
                    for (int i = 0; i < w; i++)
                        cov[i] = (uint8_t)mime_vec_div255(cov[i] * (brow[i] >> 24));
                }
                if (opacity < 255) {
                    for (int i = 0; i < w; i++)
                        cov[i] = (uint8_t)mime_vec_div255(cov[i] * opacity);
                }
                if (rt->clip_set)
                    mime_vec_mask_apply_row(&rt->clip, y, cb.left, w, cov);
                if (layer->ignore_alpha) {
                    uint32_t *s = D2dSurfaceRow(src, y) + cb.left;
                    for (int i = 0; i < w; i++)
                        s[i] |= 0xff000000u;
                }
                blend_span(rt, D2dSurfaceRow(rt->draw, y) + cb.left, D2dSurfaceRow(src, y) + cb.left, cov, w);
            }
        }
        D2dFree(cov);
        D2dFree(brow);
        if (have_brush)
            D2dPaintRelease(&brush);
    }
    if (elsewhere) {
        rt->draw = current;
        if (D2dBitmapOf(layer->target))
            D2dBitmapCommit(D2dBitmapOf(layer->target));
    }
    D2D_RELEASE(layer->brush);
    mime_vec_mask_free(&layer->mask);
    D2dSurfaceFree(&layer->surface);
    D2dFree(layer);
}

/* ------------------------------------------------------------------ clearing */

void D2dClearSurface(D2dRt *rt, const D2D1_COLOR_F *color)
{
    D2D1_COLOR_F c = color ? *color : (D2D1_COLOR_F){0, 0, 0, 0};
    if (rt->pixel_format.alphaMode == D2D1_ALPHA_MODE_IGNORE)
        c.a = 1.0f;
    uint32_t px = D2dPackPremul(c.r, c.g, c.b, c.a);
    MimeVecRect cb;
    if (!clip_bounds(rt, &cb))
        return;
    int w = cb.right - cb.left;
    uint8_t *cov = D2dAlloc((SIZE_T)w);
    uint32_t *src = D2dAlloc((SIZE_T)w * 4);
    if (!cov || !src) {
        D2dFree(cov);
        D2dFree(src);
        return;
    }
    for (int i = 0; i < w; i++)
        src[i] = px;
    for (int y = cb.top; y < cb.bottom; y++) {
        uint32_t *dst = D2dSurfaceRow(rt->draw, y) + cb.left;
        if (!rt->clip_set) {
            for (int i = 0; i < w; i++)
                dst[i] = px;
            continue;
        }
        for (int i = 0; i < w; i++)
            cov[i] = 255;
        mime_vec_mask_apply_row(&rt->clip, y, cb.left, w, cov);
        mime_vec_blend_row(dst, src, cov, (uint32_t)w, 255, MIME_VEC_BLEND_COPY, MIME_VEC_FMT_PARGB, MIME_VEC_FMT_PARGB);
    }
    D2dFree(cov);
    D2dFree(src);
}

/* ------------------------------------------------------------------ text */

static IDWriteFactory *dwrite_factory(D2dRt *rt)
{
    if (!rt->dwrite) {
        IUnknown *f = nullptr;
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, &f)))
            rt->dwrite = (IDWriteFactory *)f;
    }
    return rt->dwrite;
}

void D2dDrawGlyphRun(D2dRt *rt, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run, ID2D1Brush *brush,
                     DWRITE_MEASURING_MODE measuring)
{
    if (!check_drawing(rt))
        return;
    if (!run || !run->fontFace || measuring > DWRITE_MEASURING_MODE_GDI_NATURAL) {
        fail(rt, E_INVALIDARG);
        return;
    }
    if (!run->glyphCount)
        return;
    IDWriteFactory *f = dwrite_factory(rt);
    IDWriteFactory2 *f2 = nullptr;
    if (!f || FAILED(f->lpVtbl->QueryInterface(f, &IID_IDWriteFactory2, (void **)&f2))) {
        fail(rt, E_FAIL);
        return;
    }
    D2dPaint paint;
    if (!D2dPaintSetup(rt, &paint, brush, nullptr)) {
        f2->lpVtbl->Release(f2);
        return;
    }
    D2D1_TEXT_ANTIALIAS_MODE tm = rt->state.text_aa;
    if (tm == D2D1_TEXT_ANTIALIAS_MODE_DEFAULT)
        tm = rt->pixel_format.alphaMode == D2D1_ALPHA_MODE_IGNORE ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    if (aliased_mode(rt) && rt->state.text_aa == D2D1_TEXT_ANTIALIAS_MODE_DEFAULT)
        tm = D2D1_TEXT_ANTIALIAS_MODE_ALIASED;
    D2dMatrix world;
    D2dWorldMatrix(rt, &world);
    DWRITE_MATRIX dm = {world._11, world._12, world._21, world._22, world._31, world._32};
    DWRITE_RENDERING_MODE rmode = tm == D2D1_TEXT_ANTIALIAS_MODE_ALIASED ? DWRITE_RENDERING_MODE_ALIASED : DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
    DWRITE_TEXT_ANTIALIAS_MODE aamode = tm == D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE ? DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE : DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    IDWriteGlyphRunAnalysis *an = nullptr;
    HRESULT hr = f2->lpVtbl->CreateGlyphRunAnalysis2(f2, run, &dm, rmode, measuring, DWRITE_GRID_FIT_MODE_DEFAULT, aamode, origin.x,
                                                     origin.y, &an);
    f2->lpVtbl->Release(f2);
    if (FAILED(hr)) {
        fail(rt, hr);
        D2dPaintRelease(&paint);
        return;
    }
    BOOL cleartype = tm == D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE;
    DWRITE_TEXTURE_TYPE tt = cleartype ? DWRITE_TEXTURE_CLEARTYPE_3x1 : DWRITE_TEXTURE_ALIASED_1x1;
    RECT b;
    hr = an->lpVtbl->GetAlphaTextureBounds(an, tt, &b);
    int w = b.right - b.left, h = b.bottom - b.top;
    if (SUCCEEDED(hr) && w > 0 && h > 0) {
        UINT32 bpp = cleartype ? 3 : 1;
        BYTE *tex = D2dAllocZero((SIZE_T)w * h * bpp);
        uint32_t *prow = D2dAlloc((SIZE_T)w * 4);
        uint8_t *cov = D2dAlloc((SIZE_T)w);
        MimeVecRect cb;
        if (tex && prow && cov && clip_bounds(rt, &cb) && SUCCEEDED(an->lpVtbl->CreateAlphaTexture(an, tt, &b, tex, (UINT32)(w * h * bpp)))) {
            for (int y = 0; y < h; y++) {
                int dy = b.top + y;
                if (dy < cb.top || dy >= cb.bottom)
                    continue;
                int x0 = b.left < cb.left ? cb.left - b.left : 0;
                int x1 = b.left + w > cb.right ? cb.right - b.left : w;
                if (x1 <= x0)
                    continue;
                int n = x1 - x0;
                const BYTE *src = tex + (SIZE_T)y * w * bpp;
                D2dPaintRow(rt, &paint, b.left + x0, dy, n, prow);
                uint32_t *dst = D2dSurfaceRow(rt->draw, dy) + b.left + x0;
                if (!cleartype) {
                    for (int i = 0; i < n; i++)
                        cov[i] = src[x0 + i];
                    if (rt->clip_set)
                        mime_vec_mask_apply_row(&rt->clip, dy, b.left + x0, n, cov);
                    blend_span(rt, dst, prow, cov, n);
                } else {
                    for (int i = 0; i < n; i++)
                        cov[i] = 255;
                    if (rt->clip_set)
                        mime_vec_mask_apply_row(&rt->clip, dy, b.left + x0, n, cov);
                    for (int i = 0; i < n; i++) {
                        const BYTE *c = src + (x0 + i) * 3;
                        uint32_t sp = prow[i], d = dst[i];
                        uint32_t k = cov[i];
                        uint32_t cr = mime_vec_div255(c[0] * k), cg = mime_vec_div255(c[1] * k), cb2 = mime_vec_div255(c[2] * k);
                        if (!(cr | cg | cb2))
                            continue;
                        uint32_t sa = sp >> 24;
                        uint32_t r = mime_vec_div255(((d >> 16) & 255) * (255 - mime_vec_div255(cr * sa)) + ((sp >> 16) & 255) * cr);
                        uint32_t g = mime_vec_div255(((d >> 8) & 255) * (255 - mime_vec_div255(cg * sa)) + ((sp >> 8) & 255) * cg);
                        uint32_t bl = mime_vec_div255((d & 255) * (255 - mime_vec_div255(cb2 * sa)) + (sp & 255) * cb2);
                        uint32_t mx = cr > cg ? (cr > cb2 ? cr : cb2) : (cg > cb2 ? cg : cb2);
                        uint32_t ea = mime_vec_div255(mx * sa);
                        uint32_t a = (d >> 24) + mime_vec_div255(ea * (255 - (d >> 24)));
                        dst[i] = (a << 24) | (r << 16) | (g << 8) | bl;
                    }
                }
            }
        }
        D2dFree(tex);
        D2dFree(prow);
        D2dFree(cov);
    }
    an->lpVtbl->Release(an);
    D2dPaintRelease(&paint);
}

/* the text renderer that a text layout draws through. */
typedef struct TextRenderer {
    const void *vtbl;
    D2dRt *rt;
    ID2D1Brush *brush;
    float ox, oy;
} TextRenderer;

typedef struct UnderlineRec {
    FLOAT width, thickness, offset, run_height;
    int reading, flow;
    const WCHAR *locale;
    int measuring;
} UnderlineRec;

typedef struct StrikeRec {
    FLOAT width, thickness, offset;
    int reading, flow;
    const WCHAR *locale;
    int measuring;
} StrikeRec;

static HRESULT STDMETHODCALLTYPE tr_query(void *This, REFIID riid, void **out)
{
    static const GUID iid_pixel = {0xeaf3a2da, 0xecf4, 0x4d24, {0xb6, 0x44, 0xb3, 0x4f, 0x68, 0x42, 0x02, 0x4b}};
    static const GUID iid_renderer = {0xef8a8135, 0x5cc6, 0x45fe, {0x88, 0x25, 0xc5, 0xa0, 0x72, 0x4e, 0xb8, 0x19}};
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &iid_pixel) || IsEqualGUID(riid, &iid_renderer)) {
        *out = This;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE tr_addref(void *This) { return 2; }
static ULONG STDMETHODCALLTYPE tr_release(void *This) { return 1; }
static HRESULT STDMETHODCALLTYPE tr_snap(void *This, void *ctx, BOOL *disabled)
{
    *disabled = FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tr_transform(void *This, void *ctx, DWRITE_MATRIX *m)
{
    TextRenderer *t = This;
    D2dMatrix w;
    D2dWorldMatrix(t->rt, &w);
    m->m11 = w._11 / D2dUnitScaleX(t->rt);
    m->m12 = w._12;
    m->m21 = w._21;
    m->m22 = w._22 / D2dUnitScaleY(t->rt);
    m->dx = w._31 / D2dUnitScaleX(t->rt);
    m->dy = w._32 / D2dUnitScaleY(t->rt);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tr_ppd(void *This, void *ctx, FLOAT *ppd)
{
    *ppd = D2dUnitScaleX(((TextRenderer *)This)->rt);
    return S_OK;
}
/* a drawing effect that is a brush paints its range of the text, any other leaves the text brush. */
static ID2D1Brush *effect_brush(IUnknown *effect, ID2D1Brush *fallback, BOOL *owned)
{
    ID2D1Brush *brush = nullptr;
    *owned = FALSE;
    if (effect && SUCCEEDED(effect->lpVtbl->QueryInterface(effect, &IID_ID2D1Brush, (void **)&brush)) && brush) {
        *owned = TRUE;
        return brush;
    }
    return fallback;
}

static HRESULT STDMETHODCALLTYPE tr_glyphs(void *This, void *ctx, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mm,
                                           const DWRITE_GLYPH_RUN *run, const void *desc, IUnknown *effect)
{
    TextRenderer *t = This;
    D2D1_POINT_2F o = {x, y};
    BOOL owned;
    ID2D1Brush *brush = effect_brush(effect, t->brush, &owned);
    D2dDrawGlyphRun(t->rt, o, run, brush, mm);
    if (owned)
        ((IUnknown *)brush)->lpVtbl->Release((IUnknown *)brush);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tr_underline(void *This, void *ctx, FLOAT x, FLOAT y, const UnderlineRec *u, IUnknown *effect)
{
    TextRenderer *t = This;
    D2D1_RECT_F r = {x, y + u->offset, x + u->width, y + u->offset + u->thickness};
    BOOL owned;
    ID2D1Brush *brush = effect_brush(effect, t->brush, &owned);
    D2dFillRect(t->rt, &r, brush);
    if (owned)
        ((IUnknown *)brush)->lpVtbl->Release((IUnknown *)brush);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tr_strike(void *This, void *ctx, FLOAT x, FLOAT y, const StrikeRec *u, IUnknown *effect)
{
    TextRenderer *t = This;
    D2D1_RECT_F r = {x, y + u->offset, x + u->width, y + u->offset + u->thickness};
    BOOL owned;
    ID2D1Brush *brush = effect_brush(effect, t->brush, &owned);
    D2dFillRect(t->rt, &r, brush);
    if (owned)
        ((IUnknown *)brush)->lpVtbl->Release((IUnknown *)brush);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tr_inline(void *This, void *ctx, FLOAT x, FLOAT y, IUnknown *object, BOOL sideways,
                                           BOOL rtl, IUnknown *effect)
{
    if (!object)
        return S_OK;
    typedef HRESULT(STDMETHODCALLTYPE * DrawFn)(IUnknown *, void *, IUnknown *, FLOAT, FLOAT, BOOL, BOOL, IUnknown *);
    DrawFn draw = (DrawFn)((void **)object->lpVtbl)[3];
    return draw(object, ctx, (IUnknown *)This, x, y, sideways, rtl, effect);
}

static const void *const g_tr_vtbl[] = {tr_query, tr_addref, tr_release, tr_snap, tr_transform, tr_ppd,
                                        tr_glyphs, tr_underline, tr_strike, tr_inline};

void D2dDrawTextLayout(D2dRt *rt, D2D1_POINT_2F origin, IDWriteTextLayout *layout, ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options)
{
    if (!check_drawing(rt))
        return;
    if (!layout || !brush) {
        fail(rt, E_INVALIDARG);
        return;
    }
    TextRenderer tr = {g_tr_vtbl, rt, brush, 0, 0};
    typedef HRESULT(STDMETHODCALLTYPE * LayoutDraw)(IDWriteTextLayout *, void *, void *, FLOAT, FLOAT);
    LayoutDraw draw = (LayoutDraw)((void **)*(void **)layout)[58];
    HRESULT hr = draw(layout, nullptr, &tr, origin.x, origin.y);
    if (FAILED(hr))
        fail(rt, hr);
}

void D2dDrawText(D2dRt *rt, const WCHAR *string, UINT32 length, IDWriteTextFormat *format, const D2D1_RECT_F *rect,
                 ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring)
{
    if (!check_drawing(rt))
        return;
    if (!string || !format || !rect || !brush) {
        fail(rt, E_INVALIDARG);
        return;
    }
    IDWriteFactory *f = dwrite_factory(rt);
    IDWriteTextLayout *layout = nullptr;
    HRESULT hr = f ? f->lpVtbl->CreateTextLayout(f, string, length, format, rect->right - rect->left, rect->bottom - rect->top, &layout) : E_FAIL;
    if (FAILED(hr)) {
        fail(rt, hr);
        return;
    }
    if (options & D2D1_DRAW_TEXT_OPTIONS_CLIP)
        D2dPushAxisAlignedClip(rt, rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    D2D1_POINT_2F o = {rect->left, rect->top};
    D2dDrawTextLayout(rt, o, layout, brush, options);
    if (options & D2D1_DRAW_TEXT_OPTIONS_CLIP)
        D2dPopAxisAlignedClip(rt);
    ((IUnknown *)layout)->lpVtbl->Release((IUnknown *)layout);
}

/* the outline of a glyph run, into a path in the units of the run. */
typedef struct PathSink {
    const void *vtbl;
    D2dPath *path;
    BOOL open;
} PathSink;

static HRESULT STDMETHODCALLTYPE ps_query(void *This, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID2D1SimplifiedGeometrySink)) {
        *out = This;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ps_ref(void *This) { return 2; }
static void STDMETHODCALLTYPE ps_fill(void *This, D2D1_FILL_MODE m) { ((PathSink *)This)->path->fill_mode = m; }
static void STDMETHODCALLTYPE ps_flags(void *This, D2D1_PATH_SEGMENT f) {}
static void STDMETHODCALLTYPE ps_begin(void *This, D2D1_POINT_2F p, D2D1_FIGURE_BEGIN b)
{
    D2dPathBegin(((PathSink *)This)->path, p, b);
}
static void STDMETHODCALLTYPE ps_lines(void *This, const D2D1_POINT_2F *p, UINT32 n)
{
    for (UINT32 i = 0; i < n; i++)
        D2dPathLine(((PathSink *)This)->path, p[i]);
}
static void STDMETHODCALLTYPE ps_beziers(void *This, const D2D1_BEZIER_SEGMENT *b, UINT32 n)
{
    for (UINT32 i = 0; i < n; i++)
        D2dPathBezier(((PathSink *)This)->path, b[i].point1, b[i].point2, b[i].point3);
}
static void STDMETHODCALLTYPE ps_end(void *This, D2D1_FIGURE_END e) { D2dPathEnd(((PathSink *)This)->path, e); }
static HRESULT STDMETHODCALLTYPE ps_close(void *This) { return S_OK; }
static const void *const g_ps_vtbl[] = {ps_query, ps_ref, ps_ref, ps_fill, ps_flags, ps_begin, ps_lines, ps_beziers, ps_end, ps_close};

HRESULT D2dGlyphRunToPath(IDWriteFontFace *face, float em_size, const UINT16 *glyphs, const FLOAT *advances,
                          const DWRITE_GLYPH_OFFSET *offsets, UINT32 count, BOOL sideways, BOOL rtl, D2dPath *path)
{
    PathSink s = {g_ps_vtbl, path, FALSE};
    return face->lpVtbl->GetGlyphRunOutline(face, em_size, glyphs, advances, offsets, count, sideways, rtl, (IDWriteGeometrySink *)&s);
}
