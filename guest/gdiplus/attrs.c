/*
 * image attributes: the color adjustments applied to the pixels of an image, a brush, a pen or text while it is
 * drawn, and the wrap mode that decides what lies beyond the edge of a picture.
 */
#include "gp_private.h"

/* each of the settings is kept for the five kinds of object, and the default kind stands in for the rest. */
typedef struct AdjustSet {
    BOOL matrix_on;
    ColorMatrixFlags matrix_flags;
    float matrix[5][5];
    float gray[5][5];
    BOOL threshold_on;
    float threshold;
    BOOL gamma_on;
    float gamma;
    BOOL noop_on;
    BOOL key_on;
    ARGB key_low, key_high;
    BOOL channel_on;
    ColorChannelFlags channel;
    BOOL remap_on;
    UINT remap_count;
    ColorMap *remap;
    BOOL profile_on;
} AdjustSet;

typedef struct GAttrs {
    DWORD tag;
    AdjustSet set[ColorAdjustTypeCount];
    WrapMode wrap;
    ARGB clamp_color;
    BOOL clamp_edge;
    BOOL icm;
    BOOL cached_background;
} GAttrs;

struct GAttrs *gp_attrs_cast(const void *p) { return GP_CAST(GAttrs, GP_TAG_ATTRS, p); }

static void set_clear(AdjustSet *s)
{
    gp_free(s->remap);
    memset(s, 0, sizeof(*s));
}

void gp_attrs_free(GAttrs *a)
{
    if (!a)
        return;
    for (int i = 0; i < ColorAdjustTypeCount; i++)
        gp_free(a->set[i].remap);
    a->tag = 0;
    gp_free(a);
}

GAttrs *gp_attrs_clone(const GAttrs *a)
{
    GAttrs *c = gp_dup(a, sizeof(*a));
    if (!c)
        return nullptr;
    for (int i = 0; i < ColorAdjustTypeCount; i++) {
        if (c->set[i].remap) {
            c->set[i].remap = gp_dup(a->set[i].remap, sizeof(ColorMap) * a->set[i].remap_count);
            if (!c->set[i].remap) {
                c->set[i].remap_on = FALSE;
                gp_attrs_free(c);
                return nullptr;
            }
        }
    }
    return c;
}

WrapMode gp_attrs_wrap(const GAttrs *a, ARGB *clamp_color)
{
    if (!a) {
        if (clamp_color)
            *clamp_color = 0;
        return WrapModeClamp;
    }
    if (clamp_color)
        *clamp_color = a->clamp_color;
    return a->wrap;
}

/* the set that applies to a kind of object: its own, or the default when it has no setting of that sort. */
#define PICK(a, type, flag) ((type) > 0 && (type) < ColorAdjustTypeCount && (a)->set[type].flag ? &(a)->set[type] : &(a)->set[0])

BOOL gp_attrs_active(const GAttrs *a, ColorAdjustType type)
{
    if (!a || (UINT)type >= ColorAdjustTypeCount)
        return FALSE;
    const AdjustSet *own = &a->set[type], *def = &a->set[0];
    if (own->noop_on)
        return FALSE;
    return own->matrix_on || own->threshold_on || own->gamma_on || own->key_on || own->channel_on || own->remap_on ||
           (type != 0 && !def->noop_on &&
            (def->matrix_on || def->threshold_on || def->gamma_on || def->key_on || def->channel_on || def->remap_on));
}

static BYTE clamp_byte(float v)
{
    if (!(v > 0.0f))
        return 0;
    if (v >= 255.0f)
        return 255;
    return (BYTE)(v + 0.5f);
}

static UINT32 matrix_pixel(const AdjustSet *s, UINT32 c)
{
    float in[5] = {(float)((c >> 16) & 255), (float)((c >> 8) & 255), (float)(c & 255), (float)(c >> 24), 1.0f};
    BOOL gray = ((c >> 16) & 255) == ((c >> 8) & 255) && ((c >> 8) & 255) == (c & 255);
    const float(*m)[5] = s->matrix;
    if (gray && s->matrix_flags == ColorMatrixFlagsSkipGrays)
        return c;
    if (gray && s->matrix_flags == ColorMatrixFlagsAltGray)
        m = s->gray;
    float out[4];
    for (int col = 0; col < 4; col++) {
        float sum = in[4] * 255.0f * m[4][col];
        for (int row = 0; row < 4; row++)
            sum += in[row] * m[row][col];
        out[col] = sum;
    }
    return (UINT32)clamp_byte(out[3]) << 24 | (UINT32)clamp_byte(out[0]) << 16 | (UINT32)clamp_byte(out[1]) << 8 |
           clamp_byte(out[2]);
}

void gp_attrs_apply(const GAttrs *a, ColorAdjustType type, UINT32 *argb, INT count)
{
    if (!a || (UINT)type >= ColorAdjustTypeCount)
        return;
    if (a->set[type].noop_on)
        return;
    const AdjustSet *key = PICK(a, type, key_on), *remap = PICK(a, type, remap_on), *mat = PICK(a, type, matrix_on),
                    *thr = PICK(a, type, threshold_on), *gam = PICK(a, type, gamma_on),
                    *chan = PICK(a, type, channel_on);
    for (INT i = 0; i < count; i++) {
        UINT32 c = argb[i];
        if (key->key_on) {
            UINT32 lo = key->key_low, hi = key->key_high;
            BOOL inside = TRUE;
            for (int shift = 0; shift < 32; shift += 8) {
                UINT32 v = (c >> shift) & 255, l = (lo >> shift) & 255, h = (hi >> shift) & 255;
                if (v < l || v > h)
                    inside = FALSE;
            }
            if (inside) {
                argb[i] = 0;
                continue;
            }
        }
        if (remap->remap_on) {
            for (UINT k = 0; k < remap->remap_count; k++) {
                if (remap->remap[k].oldColor.Value == c) {
                    c = remap->remap[k].newColor.Value;
                    break;
                }
            }
        }
        if (mat->matrix_on)
            c = matrix_pixel(mat, c);
        if (thr->threshold_on) {
            float t = thr->threshold * 255.0f;
            UINT32 r = ((c >> 16) & 255) >= t ? 255 : 0, g = ((c >> 8) & 255) >= t ? 255 : 0, b = (c & 255) >= t ? 255 : 0;
            c = (c & 0xff000000u) | r << 16 | g << 8 | b;
        }
        if (gam->gamma_on && gam->gamma > 0.0f) {
            double inv = 1.0 / gam->gamma;
            UINT32 out = c & 0xff000000u;
            for (int shift = 0; shift < 24; shift += 8)
                out |= (UINT32)clamp_byte((float)(255.0 * gp_pow(((c >> shift) & 255) / 255.0, inv))) << shift;
            c = out;
        }
        if (chan->channel_on) {
            float r = ((c >> 16) & 255) / 255.0f, g = ((c >> 8) & 255) / 255.0f, b = (c & 255) / 255.0f;
            float k = 1.0f - (r > g ? (r > b ? r : b) : (g > b ? g : b));
            float v;
            switch (chan->channel) {
            case ColorChannelFlagsC:
                v = k < 1.0f ? (1.0f - r - k) / (1.0f - k) : 0.0f;
                break;
            case ColorChannelFlagsM:
                v = k < 1.0f ? (1.0f - g - k) / (1.0f - k) : 0.0f;
                break;
            case ColorChannelFlagsY:
                v = k < 1.0f ? (1.0f - b - k) / (1.0f - k) : 0.0f;
                break;
            default:
                v = k;
                break;
            }
            BYTE gray = clamp_byte(255.0f * (1.0f - v));
            c = (c & 0xff000000u) | (UINT32)gray << 16 | (UINT32)gray << 8 | gray;
        }
        argb[i] = c;
    }
}

/* ------------------------------------------------------------------ api */

static GpStatus with_set(GpImageAttributes *p, ColorAdjustType type, AdjustSet **s)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a || (UINT)type >= ColorAdjustTypeCount)
        return InvalidParameter;
    *s = &a->set[type];
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateImageAttributes(GpImageAttributes **out)
{
    if (!out)
        return InvalidParameter;
    GAttrs *a = gp_alloc_zero(sizeof(*a));
    if (!a)
        return OutOfMemory;
    a->tag = GP_TAG_ATTRS;
    a->wrap = WrapModeClamp;
    *out = a;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneImageAttributes(const GpImageAttributes *p, GpImageAttributes **out)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a || !out)
        return InvalidParameter;
    GAttrs *c = gp_attrs_clone(a);
    if (!c)
        return OutOfMemory;
    *out = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDisposeImageAttributes(GpImageAttributes *p)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a)
        return InvalidParameter;
    gp_attrs_free(a);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesToIdentity(GpImageAttributes *p, ColorAdjustType type)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st == Ok) {
        /* every adjustment of the kind is switched off while its values stay */
        s->matrix_on = s->threshold_on = s->gamma_on = s->noop_on = s->key_on = s->channel_on = s->remap_on = FALSE;
    }
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipResetImageAttributes(GpImageAttributes *p, ColorAdjustType type)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st == Ok)
        set_clear(s);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesColorMatrix(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                             const ColorMatrix *matrix, const ColorMatrix *gray,
                                                             ColorMatrixFlags flags)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st != Ok)
        return st;
    if (enable) {
        if (!matrix || (UINT)flags > ColorMatrixFlagsAltGray || (flags == ColorMatrixFlagsAltGray && !gray))
            return InvalidParameter;
        memcpy(s->matrix, matrix->m, sizeof(s->matrix));
        if (flags == ColorMatrixFlagsAltGray)
            memcpy(s->gray, gray->m, sizeof(s->gray));
        s->matrix_flags = flags;
    }
    s->matrix_on = enable != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesThreshold(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                           REAL threshold)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st != Ok)
        return st;
    s->threshold = threshold;
    s->threshold_on = enable != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesGamma(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                       REAL gamma)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st != Ok)
        return st;
    if (enable && !(gamma > 0.0f))
        return InvalidParameter;
    s->gamma = gamma;
    s->gamma_on = enable != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesNoOp(GpImageAttributes *p, ColorAdjustType type, BOOL enable)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st == Ok)
        s->noop_on = enable != 0;
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesColorKeys(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                           ARGB low, ARGB high)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st != Ok)
        return st;
    s->key_low = low;
    s->key_high = high;
    s->key_on = enable != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesOutputChannel(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                               ColorChannelFlags channel)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st != Ok)
        return st;
    if (enable && (UINT)channel >= ColorChannelFlagsLast)
        return InvalidParameter;
    s->channel = channel;
    s->channel_on = enable != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesOutputChannelColorProfile(GpImageAttributes *p, ColorAdjustType type,
                                                                           BOOL enable, const WCHAR *file)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st != Ok)
        return st;
    if (enable) {
        if (!file)
            return InvalidParameter;
        DWORD attrs = GetFileAttributesW(file);
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return InvalidParameter;
    }
    s->profile_on = enable != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesRemapTable(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                            UINT count, const ColorMap *map)
{
    AdjustSet *s;
    GpStatus st = with_set(p, type, &s);
    if (st != Ok)
        return st;
    if (enable) {
        if (!count || !map)
            return InvalidParameter;
        ColorMap *copy = gp_dup(map, sizeof(ColorMap) * count);
        if (!copy)
            return OutOfMemory;
        gp_free(s->remap);
        s->remap = copy;
        s->remap_count = count;
    }
    s->remap_on = enable != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesWrapMode(GpImageAttributes *p, WrapMode wrap, ARGB color, BOOL clamp)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a || (UINT)wrap > WrapModeClamp)
        return InvalidParameter;
    a->wrap = wrap;
    a->clamp_color = color;
    a->clamp_edge = clamp != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesICMMode(GpImageAttributes *p, BOOL on)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a)
        return InvalidParameter;
    a->icm = on != 0;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageAttributesAdjustedPalette(GpImageAttributes *p, ColorPalette *palette,
                                                                 ColorAdjustType type)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a || !palette || (UINT)type >= ColorAdjustTypeCount)
        return InvalidParameter;
    gp_attrs_apply(a, type, (UINT32 *)palette->Entries, (INT)palette->Count);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesCachedBackground(GpImageAttributes *p, BOOL enable)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a)
        return InvalidParameter;
    a->cached_background = enable != 0;
    return Ok;
}
