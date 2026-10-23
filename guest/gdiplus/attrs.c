/*
 * image attributes: the color adjustments applied while an image, brush, pen or text is drawn, and the wrap mode
 * for what lies beyond a picture's edge.
 */
#include "gp_private.h"

#define INT_MIN_VALUE (-2147483647 - 1)
#define FLOAT_EPSILON 1.1920929e-7f

enum {
    ADJ_NOOP = 0x01,
    ADJ_MATRIX = 0x02,
    ADJ_THRESHOLD = 0x04,
    ADJ_GAMMA = 0x08,
    ADJ_KEYS = 0x10,
    ADJ_REMAP = 0x20,
    ADJ_CHANNEL = 0x40,
    ADJ_GRAY_MATRIX = 0x80,
    ADJ_PROFILE = 0x100,
};

/* picked from the matrix values. */
enum {
    MATRIX_NONE,
    MATRIX_FULL,
    MATRIX_SCALE,
    MATRIX_TRANSLATE,
};

/* one per ColorAdjustType. */
typedef struct AdjustSet {
    UINT flags;
    ColorMatrixFlags matrix_flags;
    float matrix[25];
    float gray[25];
    float threshold;
    float gamma;
    ARGB key_low, key_high;
    ColorChannelFlags channel;
    UINT remap_count;
    ColorMap *remap;
    /* derived by prepare. */
    UINT matrix_kind;
    BOOL lut_on;
    int shift[4];            /* matrix translation, R, G, B, A order. */
    BYTE scale[4][256];      /* R, G, B, A. */
    BYTE lut[256];           /* gamma and threshold. */
    UINT32 gray_table[256];  /* replacements for gray pixels when there is a gray matrix. */
} AdjustSet;

typedef struct GAttrs {
    DWORD tag;
    /* a type without its own set uses the default one, unless it was set to identity. */
    AdjustSet *set[ColorAdjustTypeCount];
    BOOL identity[ColorAdjustTypeCount];
    BOOL cached_background;
    WrapMode wrap;
    ARGB clamp_color;
    BOOL clamp_edge;
} GAttrs;

struct GAttrs *gp_attrs_cast(const void *p) { return GP_CAST(GAttrs, GP_TAG_ATTRS, p); }

static void set_free(AdjustSet *s)
{
    if (s)
        gp_free(s->remap);
    gp_free(s);
}

void gp_attrs_free(GAttrs *a)
{
    if (!a)
        return;
    for (int i = 0; i < ColorAdjustTypeCount; i++)
        set_free(a->set[i]);
    a->tag = 0;
    gp_free(a);
}

GAttrs *gp_attrs_clone(const GAttrs *a)
{
    GAttrs *c = gp_alloc_zero(sizeof(*c));
    if (!c)
        return nullptr;
    *c = *a;
    memset(c->set, 0, sizeof(c->set));
    for (int i = 0; i < ColorAdjustTypeCount; i++) {
        if (!a->set[i])
            continue;
        c->set[i] = gp_dup(a->set[i], sizeof(AdjustSet));
        if (!c->set[i]) {
            gp_attrs_free(c);
            return nullptr;
        }
        c->set[i]->remap = nullptr;
        if (a->set[i]->remap) {
            c->set[i]->remap = gp_dup(a->set[i]->remap, sizeof(ColorMap) * a->set[i]->remap_count);
            if (!c->set[i]->remap) {
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

/* ------------------------------------------------------------------ numbers */

/* the processor's conversion: anything out of range becomes the lowest integer. */
static int float_to_int(float x)
{
    if (!(x >= -2147483648.0f && x < 2147483648.0f))
        return INT_MIN_VALUE;
    return (int)x;
}

static BYTE clamp_int(int v)
{
    if (v > 255)
        return 255;
    return v >= 0 ? (BYTE)v : 0;
}

static float abs_float(float v) { return v < 0.0f ? -v : v; }

static int round_half_up(float x) { return float_to_int(mime_vec_floorf(x + 0.5f)); }

/* the sums go in this order, and blue is clamped before it is rounded. */
static UINT32 matrix_pixel(const float *m, const float shift[4], UINT32 c)
{
    float r = (float)((c >> 16) & 255), g = (float)((c >> 8) & 255), b = (float)(c & 255), a = (float)(c >> 24);
    float out[4];
    for (int col = 0; col < 4; col++)
        out[col] = (((r * m[col] + g * m[5 + col]) + b * m[10 + col]) + a * m[15 + col]) + shift[col];
    BYTE nr = clamp_int(round_half_up(out[0]));
    BYTE ng = clamp_int(round_half_up(out[1]));
    BYTE na = clamp_int(round_half_up(out[3]));
    BYTE nb;
    if (out[2] < 255.0f)
        nb = out[2] > 0.0f ? clamp_int(round_half_up(out[2])) : 0;
    else
        nb = 255;
    return (UINT32)na << 24 | (UINT32)nr << 16 | (UINT32)ng << 8 | nb;
}

static BOOL is_gray(UINT32 c) { return ((c >> 16) & 255) == ((c >> 8) & 255) && ((c >> 8) & 255) == (c & 255); }

/* ------------------------------------------------------------------ preparing */

/* NaN counts as equal, since the test is written as "not different". */
static BOOL differs_from(float v, float want) { return abs_float(v - want) >= FLOAT_EPSILON; }

static void prepare_matrix(AdjustSet *s)
{
    const float *m = s->matrix;
    BOOL diagonal = TRUE, top_identity = TRUE;
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 5; col++)
            if (row != col && m[row * 5 + col] != 0.0f)
                diagonal = FALSE;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 5; col++)
            if (differs_from(m[row * 5 + col], row == col ? 1.0f : 0.0f))
                top_identity = FALSE;
    if (top_identity) {
        s->matrix_kind = MATRIX_TRANSLATE;
        for (int c = 0; c < 4; c++) {
            float shift = m[20 + c] * 255.0f;
            s->shift[c] = round_half_up(shift);
        }
        return;
    }
    if (diagonal) {
        s->matrix_kind = MATRIX_SCALE;
        float factor[4] = {m[0], m[6], m[12], differs_from(m[18], 1.0f) ? m[18] : 1.0f};
        for (int c = 0; c < 4; c++)
            for (int i = 0; i < 256; i++)
                s->scale[c][i] = clamp_int(round_half_up((float)i * factor[c]));
        return;
    }
    s->matrix_kind = MATRIX_FULL;
}

static void prepare_lut(AdjustSet *s)
{
    UINT both = s->flags & (ADJ_THRESHOLD | ADJ_GAMMA);
    s->lut_on = both != 0;
    if (both == (ADJ_THRESHOLD | ADJ_GAMMA)) {
        for (int i = 0; i < 256; i++)
            s->lut[i] = (double)s->threshold > gp_pow(i / 255.0, (double)s->gamma) ? 0 : 255;
    } else if (both == ADJ_GAMMA) {
        s->lut[0] = 0;
        s->lut[255] = 255;
        for (int i = 1; i < 255; i++) {
            float v = (float)(gp_pow(i / 255.0, (double)s->gamma) * 255.0);
            s->lut[i] = (BYTE)round_half_up(v);
        }
    } else if (both == ADJ_THRESHOLD) {
        BYTE limit = (BYTE)float_to_int(mime_vec_ceilf(s->threshold * 255.0f));
        for (int i = 0; i < 256; i++)
            s->lut[i] = i < limit ? 0 : 255;
    }
}

static void prepare(AdjustSet *s)
{
    s->matrix_kind = MATRIX_NONE;
    s->lut_on = FALSE;
    if (s->flags & ADJ_NOOP)
        return;
    if (s->flags & ADJ_MATRIX)
        prepare_matrix(s);
    if (s->flags & ADJ_GRAY_MATRIX) {
        float shift[4];
        for (int c = 0; c < 4; c++)
            shift[c] = s->gray[20 + c] * 255.0f;
        for (UINT32 i = 0; i < 256; i++)
            s->gray_table[i] = matrix_pixel(s->gray, shift, 0xff000000u | i << 16 | i << 8 | i);
    }
    prepare_lut(s);
}

/* ------------------------------------------------------------------ applying */

static const AdjustSet *pick(const GAttrs *a, ColorAdjustType type)
{
    if ((UINT)type < 1 || (UINT)type > 4)
        return nullptr;
    if (a->set[type])
        return a->set[type];
    return a->identity[type] ? nullptr : a->set[0];
}

BOOL gp_attrs_active(const GAttrs *a, ColorAdjustType type)
{
    if (!a)
        return FALSE;
    if (type == ColorAdjustTypeAny) {
        for (int i = 0; i < ColorAdjustTypeCount; i++)
            if (a->set[i] && a->set[i]->flags)
                return TRUE;
        return FALSE;
    }
    const AdjustSet *s = pick(a, type);
    return s && s->flags;
}

static void remap_pixels(const AdjustSet *s, UINT32 *argb, INT count)
{
    for (INT i = 0; i < count; i++) {
        for (UINT k = 0; k < s->remap_count; k++) {
            if (s->remap[k].oldColor.Value == argb[i]) {
                argb[i] = s->remap[k].newColor.Value;
                break;
            }
        }
    }
}

/* a pixel whose three colors are all in the key range loses its alpha. */
static void key_pixels(const AdjustSet *s, UINT32 *argb, INT count)
{
    UINT32 lo = s->key_low, hi = s->key_high;
    for (INT i = 0; i < count; i++) {
        UINT32 c = argb[i];
        BOOL inside = TRUE;
        for (int shift = 0; shift < 24; shift += 8) {
            UINT32 v = (c >> shift) & 255;
            if (v < ((lo >> shift) & 255) || v > ((hi >> shift) & 255))
                inside = FALSE;
        }
        if (inside)
            argb[i] = c & 0x00ffffffu;
    }
}

static BYTE add_shift(int shift, UINT32 v) { return clamp_int((int)((UINT)shift + v)); }

static void matrix_pixels(const AdjustSet *s, UINT32 *argb, INT count)
{
    BOOL skip_grays = s->matrix_flags == ColorMatrixFlagsSkipGrays;
    BOOL alt_grays = s->matrix_flags != ColorMatrixFlagsDefault;
    float shift[4];
    for (int c = 0; c < 4; c++)
        shift[c] = s->matrix[20 + c] * 255.0f;
    for (INT i = 0; i < count; i++) {
        UINT32 c = argb[i];
        if (alt_grays && is_gray(c)) {
            if (!skip_grays)
                argb[i] = s->gray_table[c & 255];
            continue;
        }
        switch (s->matrix_kind) {
        case MATRIX_FULL:
            argb[i] = matrix_pixel(s->matrix, shift, c);
            break;
        case MATRIX_SCALE:
            argb[i] = (UINT32)s->scale[3][c >> 24] << 24 | (UINT32)s->scale[0][(c >> 16) & 255] << 16 |
                      (UINT32)s->scale[1][(c >> 8) & 255] << 8 | s->scale[2][c & 255];
            break;
        default:
            argb[i] = (UINT32)add_shift(s->shift[3], c >> 24) << 24 |
                      (UINT32)add_shift(s->shift[0], (c >> 16) & 255) << 16 |
                      (UINT32)add_shift(s->shift[1], (c >> 8) & 255) << 8 | add_shift(s->shift[2], c & 255);
            break;
        }
    }
}

/* the gray a CMYK separation channel would be printed with. */
static UINT32 separate_channel(UINT32 c, ColorChannelFlags channel)
{
    int part[3] = {255 - (int)((c >> 16) & 255), 255 - (int)((c >> 8) & 255), 255 - (int)(c & 255)};
    int black = part[0] < part[1] ? part[0] : part[1];
    if (part[2] < black)
        black = part[2];
    int ink = channel == ColorChannelFlagsK ? black : part[channel] - black;
    UINT32 gray = (UINT32)(255 - ink);
    return (c & 0xff000000u) | gray << 16 | gray << 8 | gray;
}

void gp_attrs_apply(const GAttrs *a, ColorAdjustType type, UINT32 *argb, INT count)
{
    const AdjustSet *s = a ? pick(a, type) : nullptr;
    if (!s || !s->flags)
        return;
    if (s->flags & ADJ_REMAP)
        remap_pixels(s, argb, count);
    if (s->flags & ADJ_KEYS)
        key_pixels(s, argb, count);
    if (s->matrix_kind != MATRIX_NONE)
        matrix_pixels(s, argb, count);
    if (s->lut_on) {
        for (INT i = 0; i < count; i++) {
            UINT32 c = argb[i];
            argb[i] = (c & 0xff000000u) | (UINT32)s->lut[(c >> 16) & 255] << 16 |
                      (UINT32)s->lut[(c >> 8) & 255] << 8 | s->lut[c & 255];
        }
    }
    if (s->flags & ADJ_CHANNEL)
        for (INT i = 0; i < count; i++)
            argb[i] = separate_channel(argb[i], s->channel);
}

/* ------------------------------------------------------------------ api */

/* creates the set when missing. NULL for a type that does not exist. */
static AdjustSet *set_for_write(GAttrs *a, ColorAdjustType type)
{
    if ((UINT)type > 4)
        return nullptr;
    if (!a->set[type]) {
        AdjustSet *s = gp_alloc_zero(sizeof(*s));
        if (!s)
            return nullptr;
        s->key_low = s->key_high = 0xff000000u;
        a->set[type] = s;
        a->identity[type] = FALSE;
    }
    return a->set[type];
}

/* does nothing for a type without a set. */
static void set_clear_flags(GAttrs *a, ColorAdjustType type, UINT flags)
{
    AdjustSet *s = a->set[type];
    if (s) {
        s->flags &= ~flags;
        prepare(s);
    }
}

static GAttrs *attrs_of(GpImageAttributes *p, ColorAdjustType type)
{
    GAttrs *a = gp_attrs_cast(p);
    return a && (UINT)type <= 4 ? a : nullptr;
}

WINGDIPAPI GpStatus WINAPI GdipCreateImageAttributes(GpImageAttributes **out)
{
    GP_REQUIRE_INIT();
    if (!out)
        return InvalidParameter;
    GAttrs *a = gp_alloc_zero(sizeof(*a));
    if (!a)
        return OutOfMemory;
    a->tag = GP_TAG_ATTRS;
    a->wrap = WrapModeClamp;
    a->cached_background = TRUE;
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

/* the default's settings show through again. */
WINGDIPAPI GpStatus WINAPI GdipResetImageAttributes(GpImageAttributes *p, ColorAdjustType type)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    set_free(a->set[type]);
    a->set[type] = nullptr;
    a->identity[type] = FALSE;
    return Ok;
}

/* like a reset, but the default's settings are shut out too. */
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesToIdentity(GpImageAttributes *p, ColorAdjustType type)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    set_free(a->set[type]);
    a->set[type] = nullptr;
    a->identity[type] = TRUE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesColorMatrix(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                             const ColorMatrix *matrix, const ColorMatrix *gray,
                                                             ColorMatrixFlags flags)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_MATRIX | ADJ_GRAY_MATRIX);
        return Ok;
    }
    if (!matrix && !gray)
        return InvalidParameter;
    /* the set exists even if the arguments are wrong, which shuts the default out for the type. */
    AdjustSet *s = set_for_write(a, type);
    if (!s || !matrix)
        return InvalidParameter;
    if (flags == ColorMatrixFlagsAltGray && gray) {
        memcpy(s->matrix, matrix->m, sizeof(s->matrix));
        memcpy(s->gray, gray->m, sizeof(s->gray));
        s->flags |= ADJ_MATRIX | ADJ_GRAY_MATRIX;
        s->matrix_flags = ColorMatrixFlagsAltGray;
    } else if ((UINT)flags <= ColorMatrixFlagsSkipGrays) {
        memcpy(s->matrix, matrix->m, sizeof(s->matrix));
        s->flags = (s->flags & ~ADJ_GRAY_MATRIX) | ADJ_MATRIX;
        s->matrix_flags = flags;
    } else {
        return InvalidParameter;
    }
    prepare(s);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesThreshold(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                           REAL threshold)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_THRESHOLD);
        return Ok;
    }
    AdjustSet *s = set_for_write(a, type);
    if (!s)
        return InvalidParameter;
    s->flags |= ADJ_THRESHOLD;
    s->threshold = threshold;
    prepare(s);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesGamma(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                       REAL gamma)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_GAMMA);
        return Ok;
    }
    AdjustSet *s = set_for_write(a, type);
    if (!s || gamma <= 0.0f)
        return InvalidParameter;
    s->flags |= ADJ_GAMMA;
    s->gamma = gamma;
    prepare(s);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesNoOp(GpImageAttributes *p, ColorAdjustType type, BOOL enable)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_NOOP);
        return Ok;
    }
    AdjustSet *s = set_for_write(a, type);
    if (!s)
        return InvalidParameter;
    s->flags |= ADJ_NOOP;
    prepare(s);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesColorKeys(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                           ARGB low, ARGB high)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_KEYS);
        return Ok;
    }
    /* only the three colors are compared, not alpha. */
    if (((low >> 16) & 255) > ((high >> 16) & 255) || ((low >> 8) & 255) > ((high >> 8) & 255) ||
        (low & 255) > (high & 255))
        return InvalidParameter;
    AdjustSet *s = set_for_write(a, type);
    if (!s)
        return InvalidParameter;
    s->flags |= ADJ_KEYS;
    s->key_low = low;
    s->key_high = high;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesOutputChannel(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                               ColorChannelFlags channel)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_CHANNEL);
        return Ok;
    }
    if ((UINT)channel > ColorChannelFlagsK)
        return InvalidParameter;
    AdjustSet *s = set_for_write(a, type);
    if (!s)
        return InvalidParameter;
    s->flags |= ADJ_CHANNEL;
    s->channel = channel;
    return Ok;
}

/* the profile is checked and kept, but separation always uses plain arithmetic. */
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesOutputChannelColorProfile(GpImageAttributes *p, ColorAdjustType type,
                                                                           BOOL enable, const WCHAR *file)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_PROFILE);
        return Ok;
    }
    if (!file)
        return Win32Error;
    AdjustSet *s = set_for_write(a, type);
    if (!s)
        return Win32Error;
    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES)
        return Win32Error;
    s->flags |= ADJ_PROFILE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesRemapTable(GpImageAttributes *p, ColorAdjustType type, BOOL enable,
                                                            UINT count, const ColorMap *map)
{
    GAttrs *a = attrs_of(p, type);
    if (!a)
        return InvalidParameter;
    if (!enable) {
        set_clear_flags(a, type, ADJ_REMAP);
        return Ok;
    }
    if (!count || !map)
        return InvalidParameter;
    AdjustSet *s = set_for_write(a, type);
    if (!s)
        return InvalidParameter;
    ColorMap *copy = gp_dup(map, sizeof(ColorMap) * count);
    if (!copy)
        return InvalidParameter;
    gp_free(s->remap);
    s->remap = copy;
    s->remap_count = count;
    s->flags |= ADJ_REMAP;
    return Ok;
}

/* nothing is validated: any mode is kept and drawing handles unknown ones. */
WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesWrapMode(GpImageAttributes *p, WrapMode wrap, ARGB color, BOOL clamp)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a)
        return InvalidParameter;
    a->wrap = wrap;
    a->clamp_color = color;
    a->clamp_edge = clamp;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetImageAttributesAdjustedPalette(GpImageAttributes *p, ColorPalette *palette,
                                                                 ColorAdjustType type)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a || !palette || !palette->Count || (UINT)type - 1 > 3)
        return InvalidParameter;
    gp_attrs_apply(a, type, (UINT32 *)palette->Entries, (INT)palette->Count);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetImageAttributesCachedBackground(GpImageAttributes *p, BOOL enable)
{
    GAttrs *a = gp_attrs_cast(p);
    if (!a)
        return InvalidParameter;
    a->cached_background = enable;
    return Ok;
}
