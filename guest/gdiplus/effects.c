/*
 * the effects of gdi+ 1.1: creating them, their parameters, running them over bitmaps and the histograms.
 * docs/gdiplus.md has the rules the numbers come from.
 */
#include "fx_private.h"

enum {
    FX_BLUR,
    FX_SHARPEN,
    FX_COLOR_MATRIX,
    FX_COLOR_LUT,
    FX_BRIGHTNESS_CONTRAST,
    FX_HUE_SATURATION_LIGHTNESS,
    FX_LEVELS,
    FX_TINT,
    FX_COLOR_BALANCE,
    FX_RED_EYE,
    FX_COLOR_CURVE,
};

#define LUT_BYTES 1024
#define MATRIX_BYTES 100

typedef struct GEffect {
    DWORD tag;
    UINT kind;
    /* what Initialize was given and found out */
    FxImage input;
    BOOL initialized;
    RECT bounds;
    union {
        struct {
            float radius;
            INT expand;
        } blur;
        struct {
            float radius, amount;
        } sharpen;
        struct {
            BOOL set;
            float m[25];
        } matrix;
        INT ints[3];
        FxCurve *curve;
        FxRedEye *red_eye;
    } u;
    /* the lookup tables of the effects that work through them: blue, green, red and alpha. */
    BYTE *lut;
} GEffect;

/* ------------------------------------------------------------------ small things */

BOOL fx_rect_empty(const RECT *r) { return r->left >= r->right || r->top >= r->bottom; }

BOOL fx_intersect(RECT *out, const RECT *a, const RECT *b)
{
    RECT r = {a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
              a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom};
    if (fx_rect_empty(&r)) {
        memset(out, 0, sizeof(*out));
        return FALSE;
    }
    *out = r;
    return TRUE;
}

INT fx_float_to_int(float x)
{
    if (!(x >= -2147483648.0f && x < 2147483648.0f))
        return (-2147483647 - 1);
    return (INT)x;
}

BYTE fx_saturate(INT v)
{
    if (v > 255)
        return 255;
    return v < 0 ? 0 : (BYTE)v;
}

static INT round_half_up(float x) { return fx_float_to_int(mime_vec_floorf(x + 0.5f)); }

static GpStatus status_of(FxResult hr)
{
    switch (hr) {
    case FX_OK:
        return Ok;
    case FX_E_INVALIDARG:
        return InvalidParameter;
    case FX_E_OUTOFMEMORY:
        return OutOfMemory;
    default:
        return GenericError;
    }
}

static GEffect *effect_cast(const void *p) { return GP_CAST(GEffect, GP_TAG_EFFECT, p); }

static BOOL guid_is(const GUID *a, const GUID *b) { return memcmp(a, b, sizeof(GUID)) == 0; }

/* ------------------------------------------------------------------ colors */

/* lightness, saturation and hue of a color, each as a byte. blue, green and red are the inputs. */
static void rgb_to_hsl(INT b, INT g, INT r, BYTE *l_out, BYTE *s_out, BYTE *h_out)
{
    INT high = g > r ? g : r, low = g > r ? r : g;
    if (b > high)
        high = b;
    else if (b < low)
        low = b;
    INT sum = high + low, l = sum >> 1;
    if (high == low) {
        *l_out = (BYTE)l;
        *s_out = 0;
        *h_out = 255;
        return;
    }
    INT spread = high - low;
    INT scale = l >= 128 ? 510 - sum : sum;
    INT s = 255 * spread / scale;
    INT x;
    if (r == high)
        x = g - b;
    else if (g == high)
        x = b + 2 * spread - r;
    else
        x = r + 4 * spread - g;
    INT hue = 43 * x / spread;
    if (hue < 0)
        hue += 255;
    *l_out = (BYTE)l;
    *s_out = (BYTE)s;
    *h_out = (BYTE)(hue <= 128 ? hue : hue - 2);
}

/* the inverse, the sextants of the hue circle are 43 wide. */
static void hsl_to_rgb(INT l, INT s, INT h, BYTE *b_out, BYTE *g_out, BYTE *r_out)
{
    if (s == 0) {
        *b_out = *g_out = *r_out = (BYTE)l;
        return;
    }
    INT t = s * l / 255;
    if (l >= 128)
        t = s - t;
    INT top = (BYTE)(l + t), low = 2 * l - (l + t), span = l + t - low;
    INT r, g, b;
    if (h <= 43) {
        r = top;
        g = low + span * h / 43;
        b = low;
    } else if (h <= 85) {
        r = low + span * (85 - h) / 43;
        g = top;
        b = low;
    } else if (h <= 128) {
        r = low;
        g = top;
        b = low + span * (h - 85) / 43;
    } else if (h <= 170) {
        r = low;
        g = low + span * (170 - h) / 43;
        b = top;
    } else if (h <= 213) {
        r = low + span * (h - 170) / 43;
        g = low;
        b = top;
    } else {
        r = top;
        g = low;
        b = low + span * (255 - h) / 43;
    }
    *b_out = (BYTE)b;
    *g_out = (BYTE)g;
    *r_out = (BYTE)r;
}

/* ------------------------------------------------------------------ lookup tables */

static void lut_identity(BYTE *lut)
{
    for (INT i = 0; i < 256; i++)
        lut[i] = lut[256 + i] = lut[512 + i] = lut[768 + i] = (BYTE)i;
}

static void lut_brightness_contrast(const GEffect *e, BYTE *lut)
{
    INT brightness = e->u.ints[0], contrast = e->u.ints[1];
    if (!brightness && !contrast)
        return;
    float c = (float)contrast / 100.0f;
    float b = ((float)brightness + 255.0f) / 510.0f;
    if (c >= 1.0f)
        c = 0.99999899f;
    float rest = 1.0f - b;
    if (c > 0.0f) {
        float shift = (rest - (1.0f - c) * b) * 255.0f;
        for (INT i = 0; i < 256; i++) {
            float v = ((float)i - shift) / (1.0f - c) + 0.5f;
            lut[i] = lut[256 + i] = lut[512 + i] = fx_saturate(fx_float_to_int(mime_vec_floorf(v)));
        }
    } else {
        float gain = c + 1.0f;
        float shift = (b - rest * gain) * 255.0f;
        for (INT i = 0; i < 256; i++) {
            float v = ((float)i * gain + shift) + 0.5f;
            lut[i] = lut[256 + i] = lut[512 + i] = fx_saturate(fx_float_to_int(mime_vec_floorf(v)));
        }
    }
}

/* the tables work on the bytes the pixels are made into: lightness, saturation and hue. */
static void lut_hue_saturation_lightness(const GEffect *e, BYTE *lut)
{
    INT hue = e->u.ints[0], saturation = e->u.ints[1], lightness = e->u.ints[2];
    float lift = ((float)lightness / 100.0f) * 255.0f;
    float gain = ((float)saturation / 100.0f) + 1.0f;
    float turn = ((float)(hue < 0 ? hue + 360 : hue) * 255.0f) / 360.0f;
    for (INT i = 0; i < 256; i++) {
        float h = (float)i + turn;
        if (h <= 255.0f) {
            if (h < 0.0f)
                h += 255.0f;
        } else {
            h += -255.0f;
        }
        lut[512 + i] = (BYTE)round_half_up(h);
        lut[256 + i] = fx_saturate(round_half_up((float)i * gain));
        lut[i] = fx_saturate(round_half_up(lift + (float)i));
    }
}

/* the exponent that makes the middle of the range lighter or darker, a step per eighth of the way to the end. */
float fx_gamma_from_midtone(INT midtone)
{
    if ((UINT)(midtone + 100) > 200u)
        return 1.0f;
    float a = (midtone < 0 ? -(float)midtone : (float)midtone) / 100.0f, g;
    if (a >= 0.5f) {
        if (a >= 0.75f) {
            if (a >= 0.875f) {
                if (a >= 0.9375f) {
                    if (a >= 0.96875f) {
                        if (a >= 0.984375f)
                            g = a >= 0.9921875f ? 10.0f : a * 128.0f - 119.0f;
                        else
                            g = a * 64.0f - 56.0f;
                    } else {
                        g = a * 32.0f - 25.0f;
                    }
                } else {
                    g = a * 16.0f - 10.0f;
                }
            } else {
                g = a * 8.0f - 3.0f;
            }
        } else {
            g = a * 4.0f;
        }
    } else {
        g = a + a + 1.0f;
    }
    return (float)midtone >= 0.0f ? 1.0f / g : g;
}

static void lut_levels(const GEffect *e, BYTE *lut)
{
    float high = ((float)e->u.ints[0] * 255.0f) / 100.0f;
    float shadow = ((float)e->u.ints[2] * 255.0f) / 100.0f;
    float gamma = fx_gamma_from_midtone(e->u.ints[1]);
    float base = 0.0f, low, top;
    if (high > shadow) {
        low = shadow;
        top = high;
    } else if (shadow > high) {
        base = 255.0f;
        low = high;
        top = shadow;
    } else {
        low = high > 127.0f ? shadow + 1.0f : shadow - 1.0f;
        top = high;
    }
    if (low > top) {
        float swap = low;
        low = top;
        top = swap;
    }
    BYTE fill = (BYTE)fx_float_to_int(mime_vec_floorf(base + 0.5f));
    INT i = 0;
    if (low >= 0.0f) {
        do {
            if (i > 255)
                break;
            lut[i] = lut[256 + i] = lut[512 + i] = fill;
            i++;
        } while (low >= (float)i);
    }
    while (top > (float)i) {
        if (i > 255)
            return;
        float x = ((float)i - low) * (1.0f / (top - low));
        float power = (float)gp_pow((double)x, (double)gamma);
        float v = base - power * 255.0f;
        v = (v < 0.0f ? -v : v) + 0.5f;
        lut[i] = lut[256 + i] = lut[512 + i] = fx_saturate(fx_float_to_int(mime_vec_floorf(v)));
        i++;
    }
    for (; i < 256; i++)
        lut[i] = lut[256 + i] = lut[512 + i] = (BYTE)~fill;
}

static void lut_color_balance(const GEffect *e, BYTE *lut)
{
    float red = ((float)e->u.ints[0] / 100.0f) + 1.0f;
    float green = ((float)e->u.ints[1] / 100.0f) + 1.0f;
    float blue = ((float)e->u.ints[2] / 100.0f) + 1.0f;
    for (INT i = 0; i < 256; i++) {
        float scaled[3] = {(float)i * blue, (float)i * green, (float)i * red};
        for (INT t = 0; t < 3; t++)
            lut[256 * t + i] = scaled[t] >= 255.0f ? 255 : (BYTE)round_half_up(scaled[t]);
    }
}

/* the table of an effect that has one, made when there is none. */
static FxResult lut_ensure(GEffect *e)
{
    if (e->lut)
        return FX_OK;
    e->lut = gp_alloc(LUT_BYTES);
    if (!e->lut)
        return FX_E_OUTOFMEMORY;
    lut_identity(e->lut);
    return FX_OK;
}

static BOOL has_lut(UINT kind)
{
    return kind == FX_COLOR_LUT || kind == FX_BRIGHTNESS_CONTRAST || kind == FX_HUE_SATURATION_LIGHTNESS ||
           kind == FX_LEVELS || kind == FX_COLOR_BALANCE || kind == FX_COLOR_CURVE;
}

static void lut_generate(GEffect *e)
{
    switch (e->kind) {
    case FX_BRIGHTNESS_CONTRAST:
        lut_brightness_contrast(e, e->lut);
        break;
    case FX_HUE_SATURATION_LIGHTNESS:
        lut_hue_saturation_lightness(e, e->lut);
        break;
    case FX_LEVELS:
        lut_levels(e, e->lut);
        break;
    case FX_COLOR_BALANCE:
        lut_color_balance(e, e->lut);
        break;
    case FX_COLOR_CURVE:
        fx_curve_tables(e->u.curve, e->lut);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ per pixel work */

static void apply_lut_row(const BYTE *lut, const UINT32 *src, UINT32 *dst, INT n)
{
    for (INT i = 0; i < n; i++) {
        UINT32 c = src[i];
        dst[i] = (UINT32)lut[768 + (c >> 24)] << 24 | (UINT32)lut[512 + ((c >> 16) & 255)] << 16 |
                 (UINT32)lut[256 + ((c >> 8) & 255)] << 8 | lut[c & 255];
    }
}

static void apply_hsl_row(const BYTE *lut, const UINT32 *src, UINT32 *dst, INT n)
{
    for (INT i = 0; i < n; i++) {
        UINT32 c = src[i];
        BYTE l, s, h;
        rgb_to_hsl((INT)(c & 255), (INT)((c >> 8) & 255), (INT)((c >> 16) & 255), &l, &s, &h);
        BYTE b, g, r;
        hsl_to_rgb(lut[l], lut[256 + s], lut[512 + h], &b, &g, &r);
        dst[i] = (c & 0xff000000u) | (UINT32)r << 16 | (UINT32)g << 8 | b;
    }
}

/* the matrix on straight pixels, every channel rounded and clamped the same way. */
static void apply_matrix_row(const float *m, const UINT32 *src, UINT32 *dst, INT n)
{
    float shift[4];
    for (INT c = 0; c < 4; c++)
        shift[c] = m[20 + c] * 255.0f;
    for (INT i = 0; i < n; i++) {
        UINT32 c = src[i];
        float r = (float)((c >> 16) & 255), g = (float)((c >> 8) & 255), b = (float)(c & 255), a = (float)(c >> 24);
        BYTE out[4];
        for (INT col = 0; col < 4; col++) {
            float sum = (((r * m[col] + g * m[5 + col]) + b * m[10 + col]) + a * m[15 + col]) + shift[col];
            out[col] = fx_saturate(round_half_up(sum));
        }
        dst[i] = (UINT32)out[3] << 24 | (UINT32)out[0] << 16 | (UINT32)out[1] << 8 | out[2];
    }
}

typedef struct TintPlan {
    INT amount;      /* the strength, 0..255 and negative for the opposite */
    INT red, green, blue;
} TintPlan;

static void tint_plan(const GEffect *e, TintPlan *plan)
{
    INT hue = e->u.ints[0], amount = e->u.ints[1];
    plan->amount = round_half_up((float)amount * 2.55f);
    BYTE hue_byte = (BYTE)round_half_up(((float)hue * 255.0f) / 360.0f);
    BYTE b, g, r;
    hsl_to_rgb(0x7f, 0xff, hue_byte, &b, &g, &r);
    plan->red = r;
    plan->green = g;
    plan->blue = b;
}

/* the colors move toward the tint by the amount and the brightness of the pixel stays what it was. */
static void apply_tint_row(const TintPlan *plan, const UINT32 *src, UINT32 *dst, INT n)
{
    INT keep = 255 - plan->amount;
    for (INT i = 0; i < n; i++) {
        UINT32 c = src[i];
        INT r = (INT)((c >> 16) & 255), g = (INT)((c >> 8) & 255), b = (INT)(c & 255);
        INT top = g > r ? g : r;
        if (b > top)
            top = b;
        INT pull = (plan->amount * top) >> 8;
        INT mix_r = plan->red * pull + keep * r;
        INT mix_g = plan->green * pull + keep * g;
        INT mix_b = plan->blue * pull + keep * b;
        INT shift = 19 * b + 54 * r + 183 * g - 54 * (mix_r >> 8) - 183 * (mix_g >> 8) - 19 * (mix_b >> 8);
        dst[i] = (c & 0xff000000u) | (UINT32)fx_saturate((shift + mix_r) >> 8) << 16 |
                 (UINT32)fx_saturate((shift + mix_g) >> 8) << 8 | fx_saturate((shift + mix_b) >> 8);
    }
}

/* ------------------------------------------------------------------ the transform */

typedef struct FxCaps {
    BOOL in_place;       /* the destination may be the source */
    BOOL aux;            /* has data for the program besides the pixels */
    BOOL rgb24;          /* works on 24 bit pixels as they are */
} FxCaps;

static FxCaps caps_of(UINT kind)
{
    FxCaps caps = {TRUE, FALSE, FALSE};
    if (kind == FX_BLUR || kind == FX_SHARPEN)
        caps.in_place = FALSE;
    if (has_lut(kind)) {
        caps.aux = TRUE;
        caps.rgb24 = TRUE;
    }
    if (kind == FX_TINT)
        caps.rgb24 = TRUE;
    return caps;
}

/* the format a bitmap is processed in: its own when the effect takes it, else 32 bit with alpha. */
static PixelFormat best_format(UINT kind, PixelFormat source)
{
    FxCaps caps = caps_of(kind);
    if (source == PixelFormat32bppARGB || (caps.rgb24 && source == PixelFormat24bppRGB))
        return source;
    return PixelFormat32bppARGB;
}

static FxResult effect_initialize(GEffect *e, const FxImage *input)
{
    e->input = *input;
    e->initialized = TRUE;
    e->bounds = (RECT){0, 0, input->width, input->height};
    switch (e->kind) {
    case FX_BLUR:
        if (e->u.blur.expand) {
            INT grow = fx_float_to_int(mime_vec_ceilf(e->u.blur.radius));
            e->bounds.left -= grow;
            e->bounds.top -= grow;
            e->bounds.right += grow;
            e->bounds.bottom += grow;
        }
        break;
    case FX_COLOR_MATRIX:
        if (!e->u.matrix.set) {
            memset(e->u.matrix.m, 0, sizeof(e->u.matrix.m));
            for (INT i = 0; i < 5; i++)
                e->u.matrix.m[i * 6] = 1.0f;
            e->u.matrix.set = TRUE;
        }
        break;
    default:
        if (has_lut(e->kind)) {
            FxResult hr = lut_ensure(e);
            if (hr != FX_OK)
                return hr;
            lut_generate(e);
        }
        break;
    }
    return FX_OK;
}

/* works on the part of the input in rect, dst is where the corner of it goes. */
static FxResult effect_execute(GEffect *e, const RECT *exec, UINT32 *dst, INT dst_stride)
{
    if (!dst)
        return FX_OK;
    if (!e->initialized)
        return FX_E_INVALIDARG;
    const FxImage *in = &e->input;
    RECT whole = {0, 0, in->width, in->height}, rect = whole;
    if (exec)
        fx_intersect(&rect, &whole, exec);
    INT w = rect.right - rect.left;
    const UINT32 *from = in->pixels + (SSIZE_T)rect.top * in->stride + rect.left;
    TintPlan tint;
    switch (e->kind) {
    case FX_BLUR:
        return fx_blur(in, exec, dst, dst_stride, e->u.blur.radius, e->u.blur.expand, &e->bounds);
    case FX_SHARPEN:
        return fx_sharpen(in, exec, dst, dst_stride, e->u.sharpen.radius, e->u.sharpen.amount, &e->bounds);
    case FX_RED_EYE:
        return fx_redeye_run(e->u.red_eye, in, &rect, dst, dst_stride);
    case FX_TINT:
        tint_plan(e, &tint);
        break;
    default:
        break;
    }
    for (INT y = 0; y < rect.bottom - rect.top; y++) {
        const UINT32 *s = from + (SSIZE_T)y * in->stride;
        UINT32 *d = dst + (SSIZE_T)y * dst_stride;
        switch (e->kind) {
        case FX_COLOR_MATRIX:
            apply_matrix_row(e->u.matrix.m, s, d, w);
            break;
        case FX_HUE_SATURATION_LIGHTNESS:
            apply_hsl_row(e->lut, s, d, w);
            break;
        case FX_TINT:
            apply_tint_row(&tint, s, d, w);
            break;
        default:
            apply_lut_row(e->lut, s, d, w);
            break;
        }
    }
    return FX_OK;
}

/* ------------------------------------------------------------------ the objects */

static void effect_free(GEffect *e)
{
    if (!e)
        return;
    gp_free(e->lut);
    if (e->kind == FX_COLOR_CURVE)
        fx_curve_free(e->u.curve);
    if (e->kind == FX_RED_EYE)
        fx_redeye_free(e->u.red_eye);
    e->tag = 0;
    gp_free(e);
}

WINGDIPAPI GpStatus WINAPI GdipCreateEffect(const GUID id, CGpEffect **out)
{
    if (!out)
        return InvalidParameter;
    static const struct {
        const GUID *id;
        UINT kind;
    } table[] = {
        {&BlurEffectGuid, FX_BLUR},
        {&SharpenEffectGuid, FX_SHARPEN},
        {&ColorMatrixEffectGuid, FX_COLOR_MATRIX},
        {&ColorLUTEffectGuid, FX_COLOR_LUT},
        {&BrightnessContrastEffectGuid, FX_BRIGHTNESS_CONTRAST},
        {&HueSaturationLightnessEffectGuid, FX_HUE_SATURATION_LIGHTNESS},
        {&LevelsEffectGuid, FX_LEVELS},
        {&TintEffectGuid, FX_TINT},
        {&ColorBalanceEffectGuid, FX_COLOR_BALANCE},
        {&RedEyeCorrectionEffectGuid, FX_RED_EYE},
        {&ColorCurveEffectGuid, FX_COLOR_CURVE},
    };
    for (UINT i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (!guid_is(&id, table[i].id))
            continue;
        GEffect *e = gp_alloc_zero(sizeof(*e));
        if (!e)
            return OutOfMemory;
        e->tag = GP_TAG_EFFECT;
        e->kind = table[i].kind;
        switch (e->kind) {
        case FX_BLUR:
            e->u.blur.radius = 10.0f;
            break;
        case FX_SHARPEN:
            e->u.sharpen.radius = 10.0f;
            e->u.sharpen.amount = 50.0f;
            break;
        case FX_LEVELS:
            e->u.ints[0] = 100;
            break;
        case FX_COLOR_CURVE:
            e->u.curve = fx_curve_new();
            if (!e->u.curve) {
                effect_free(e);
                return OutOfMemory;
            }
            break;
        case FX_RED_EYE:
            e->u.red_eye = fx_redeye_new();
            if (!e->u.red_eye) {
                effect_free(e);
                return OutOfMemory;
            }
            break;
        default:
            break;
        }
        *out = e;
        return Ok;
    }
    return GenericError;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteEffect(CGpEffect *effect)
{
    GEffect *e = effect_cast(effect);
    if (!e)
        return InvalidParameter;
    effect_free(e);
    return Ok;
}

static UINT param_size(const GEffect *e)
{
    switch (e->kind) {
    case FX_BLUR:
    case FX_SHARPEN:
    case FX_TINT:
    case FX_BRIGHTNESS_CONTRAST:
        return 8;
    case FX_COLOR_MATRIX:
        return MATRIX_BYTES;
    case FX_COLOR_LUT:
        return LUT_BYTES;
    case FX_RED_EYE:
        return (UINT)sizeof(RedEyeCorrectionParams) + (UINT)sizeof(RECT) * fx_redeye_count(e->u.red_eye);
    default:
        return 12;
    }
}

WINGDIPAPI GpStatus WINAPI GdipGetEffectParameterSize(CGpEffect *effect, UINT *size)
{
    GEffect *e = effect_cast(effect);
    if (!e || !size)
        return InvalidParameter;
    *size = param_size(e);
    return Ok;
}

/* stores a value if it is in range. the effects set their values one after the other and keep those done. */
static FxResult store_ranged(INT value, INT *to, INT low, INT high)
{
    if (value < low || value > high)
        return FX_E_INVALIDARG;
    *to = value;
    return FX_OK;
}

static FxResult set_parameters(GEffect *e, const void *params, UINT size)
{
    const INT *v = params;
    FxResult hr;
    switch (e->kind) {
    case FX_BLUR: {
        const float *f = params;
        if (size != 8 || !(f[0] >= 0.0f && f[0] <= 256.0f))
            return FX_E_INVALIDARG;
        e->u.blur.radius = f[0];
        e->u.blur.expand = v[1];
        return FX_OK;
    }
    case FX_SHARPEN: {
        const float *f = params;
        if (size != 8 || !(f[0] >= 0.0f && f[1] >= 0.0f && f[0] <= 256.0f && f[1] <= 100.0f))
            return FX_E_INVALIDARG;
        e->u.sharpen.radius = f[0];
        e->u.sharpen.amount = f[1];
        return FX_OK;
    }
    case FX_TINT:
        if (size != 8)
            return FX_E_INVALIDARG;
        hr = store_ranged(v[0], &e->u.ints[0], -180, 180);
        return hr != FX_OK ? hr : store_ranged(v[1], &e->u.ints[1], -100, 100);
    case FX_BRIGHTNESS_CONTRAST:
        if (size != 8)
            return FX_E_INVALIDARG;
        hr = store_ranged(v[0], &e->u.ints[0], -255, 255);
        return hr != FX_OK ? hr : store_ranged(v[1], &e->u.ints[1], -100, 100);
    case FX_HUE_SATURATION_LIGHTNESS:
        if (size != 12)
            return FX_E_INVALIDARG;
        hr = store_ranged(v[0], &e->u.ints[0], -180, 180);
        if (hr == FX_OK)
            hr = store_ranged(v[1], &e->u.ints[1], -100, 100);
        return hr != FX_OK ? hr : store_ranged(v[2], &e->u.ints[2], -100, 100);
    case FX_LEVELS:
        if (size != 12)
            return FX_E_INVALIDARG;
        hr = store_ranged(v[0], &e->u.ints[0], 0, 100);
        if (hr == FX_OK)
            hr = store_ranged(v[1], &e->u.ints[1], -100, 100);
        return hr != FX_OK ? hr : store_ranged(v[2], &e->u.ints[2], 0, 100);
    case FX_COLOR_BALANCE:
        if (size != 12)
            return FX_E_INVALIDARG;
        hr = store_ranged(v[0], &e->u.ints[0], -100, 100);
        if (hr == FX_OK)
            hr = store_ranged(v[1], &e->u.ints[1], -100, 100);
        return hr != FX_OK ? hr : store_ranged(v[2], &e->u.ints[2], -100, 100);
    case FX_COLOR_MATRIX:
        if (size != MATRIX_BYTES)
            return FX_E_INVALIDARG;
        memcpy(e->u.matrix.m, params, MATRIX_BYTES);
        e->u.matrix.set = TRUE;
        return FX_OK;
    case FX_COLOR_LUT:
        if (size != LUT_BYTES)
            return FX_E_INVALIDARG;
        if (!e->lut) {
            e->lut = gp_alloc(LUT_BYTES);
            if (!e->lut)
                return FX_E_OUTOFMEMORY;
        }
        memcpy(e->lut, params, LUT_BYTES);
        return FX_OK;
    case FX_COLOR_CURVE:
        if (size != 12)
            return FX_E_INVALIDARG;
        return fx_curve_set(e->u.curve, v);
    default: {
        const RedEyeCorrectionParams *p = params;
        if (size < sizeof(*p) || !p->numberOfAreas || p->numberOfAreas > 0xffffffe ||
            size < sizeof(*p) + (SIZE_T)p->numberOfAreas * sizeof(RECT))
            return FX_E_INVALIDARG;
        return fx_redeye_set(e->u.red_eye, p->numberOfAreas, p->areas);
    }
    }
}

WINGDIPAPI GpStatus WINAPI GdipSetEffectParameters(CGpEffect *effect, const VOID *params, UINT size)
{
    GEffect *e = effect_cast(effect);
    if (!e || !params)
        return InvalidParameter;
    return status_of(set_parameters(e, params, size));
}

static FxResult get_parameters(GEffect *e, UINT *size, void *params)
{
    if (!size)
        return FX_E_INVALIDARG;
    UINT need = param_size(e);
    if (*size < need)
        return FX_E_INVALIDARG;
    INT *v = params;
    switch (e->kind) {
    case FX_BLUR:
        ((float *)params)[0] = e->u.blur.radius;
        v[1] = e->u.blur.expand;
        break;
    case FX_SHARPEN:
        ((float *)params)[0] = e->u.sharpen.radius;
        ((float *)params)[1] = e->u.sharpen.amount;
        break;
    case FX_COLOR_MATRIX:
        /* the matrix of an effect nobody has set is the identity. */
        if (!e->u.matrix.set) {
            memset(e->u.matrix.m, 0, sizeof(e->u.matrix.m));
            for (INT i = 0; i < 5; i++)
                e->u.matrix.m[i * 6] = 1.0f;
            e->u.matrix.set = TRUE;
        }
        memcpy(params, e->u.matrix.m, MATRIX_BYTES);
        *size = MATRIX_BYTES;
        break;
    case FX_COLOR_LUT: {
        FxResult hr = lut_ensure(e);
        if (hr != FX_OK)
            return hr;
        memcpy(params, e->lut, LUT_BYTES);
        *size = LUT_BYTES;
        break;
    }
    case FX_COLOR_CURVE:
        fx_curve_get(e->u.curve, v);
        break;
    case FX_RED_EYE: {
        RedEyeCorrectionParams *p = params;
        UINT count = fx_redeye_count(e->u.red_eye);
        p->numberOfAreas = count;
        p->areas = (RECT *)(p + 1);
        if (count)
            memcpy(p->areas, fx_redeye_areas(e->u.red_eye), sizeof(RECT) * count);
        break;
    }
    default:
        for (UINT i = 0; i < need / 4; i++)
            v[i] = e->u.ints[i];
        break;
    }
    return FX_OK;
}

WINGDIPAPI GpStatus WINAPI GdipGetEffectParameters(CGpEffect *effect, UINT *size, VOID *params)
{
    GEffect *e = effect_cast(effect);
    if (!e || !params)
        return InvalidParameter;
    return status_of(get_parameters(e, size, params));
}

/* ------------------------------------------------------------------ running over bitmaps */

/* the pixels of a bitmap as straight argb: the bitmap's own memory when it is that already, else a copy. */
static FxResult image_read(GBitmap *b, FxImage *image, BOOL *copied)
{
    image->width = b->width;
    image->height = b->height;
    if (b->format == PixelFormat32bppARGB && !(b->stride & 3) && !((UINT_PTR)b->bits & 3)) {
        image->stride = b->stride / 4;
        image->pixels = (UINT32 *)b->bits;
        *copied = FALSE;
        return FX_OK;
    }
    SIZE_T count = (SIZE_T)b->width * (SIZE_T)b->height;
    image->stride = b->width;
    image->pixels = gp_alloc(sizeof(UINT32) * count);
    if (!image->pixels)
        return FX_E_OUTOFMEMORY;
    for (INT y = 0; y < b->height; y++)
        gp_bitmap_read_argb(b, 0, y, b->width, image->pixels + (SSIZE_T)y * image->stride);
    *copied = TRUE;
    return FX_OK;
}

static void image_write(GBitmap *b, const FxImage *image)
{
    for (INT y = 0; y < b->height; y++)
        gp_bitmap_write_argb(b, 0, y, b->width, image->pixels + (SSIZE_T)y * image->stride);
}

/* the table of an effect as the program gets it. */
static FxResult aux_data(GEffect *e, VOID **data, INT *size)
{
    if (!caps_of(e->kind).aux)
        return FX_OK;
    BYTE *copy = gp_alloc(LUT_BYTES);
    if (!copy)
        return FX_E_OUTOFMEMORY;
    memcpy(copy, e->lut, LUT_BYTES);
    *data = copy;
    *size = LUT_BYTES;
    return FX_OK;
}

WINGDIPAPI GpStatus WINAPI GdipBitmapApplyEffect(GpBitmap *bitmap, CGpEffect *effect, RECT *roi, BOOL use_aux,
                                                 VOID **aux, INT *aux_size)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    GEffect *e = effect_cast(effect);
    if (!b || !e)
        return InvalidParameter;
    if (b->locked)
        return GenericError;
    FxCaps caps = caps_of(e->kind);
    FxImage image, source;
    BOOL copied;
    FxResult hr = image_read(b, &image, &copied);
    if (hr != FX_OK)
        return status_of(hr);
    UINT32 *clone = nullptr;
    source = image;
    if (!caps.in_place) {
        /* the effect reads what it started with while it writes, so it reads a copy. */
        SIZE_T count = (SIZE_T)image.width * (SIZE_T)image.height;
        clone = gp_alloc(sizeof(UINT32) * count);
        if (!clone) {
            if (copied)
                gp_free(image.pixels);
            return OutOfMemory;
        }
        for (INT y = 0; y < image.height; y++)
            memcpy(clone + (SSIZE_T)y * image.width, image.pixels + (SSIZE_T)y * image.stride,
                   sizeof(UINT32) * (SIZE_T)image.width);
        source.pixels = clone;
        source.stride = image.width;
    }
    hr = effect_initialize(e, &source);
    if (hr == FX_OK) {
        RECT whole = {0, 0, image.width, image.height}, rect = e->bounds;
        if (roi)
            fx_intersect(&rect, &rect, roi);
        fx_intersect(&rect, &rect, &whole);
        if (!fx_rect_empty(&rect))
            hr = effect_execute(e, &rect, image.pixels + (SSIZE_T)rect.top * image.stride + rect.left, image.stride);
        if (hr == FX_OK && use_aux && aux && aux_size)
            hr = aux_data(e, aux, aux_size);
    }
    e->initialized = FALSE;
    if (copied) {
        image_write(b, &image);
        gp_free(image.pixels);
    }
    gp_free(clone);
    return status_of(hr);
}

/* runs the effect over one bitmap into a new one, which stays NULL when nothing of the region is left. */
static GpStatus create_applied(GEffect *e, GBitmap *b, const RECT *roi, GBitmap **output, RECT *output_rect,
                               BOOL use_aux, VOID **aux, INT *aux_size)
{
    if (b->locked)
        return GenericError;
    FxImage image;
    BOOL copied;
    FxResult hr = image_read(b, &image, &copied);
    if (hr != FX_OK)
        return status_of(hr);
    PixelFormat format = best_format(e->kind, b->format);
    *output = nullptr;
    GBitmap *result = nullptr;
    RECT rect = {0, 0, 0, 0};
    hr = effect_initialize(e, &image);
    if (hr == FX_OK) {
        rect = e->bounds;
        if (roi)
            fx_intersect(&rect, &rect, roi);
        if (!fx_rect_empty(&rect)) {
            INT w = rect.right - rect.left, h = rect.bottom - rect.top;
            GpStatus st = gp_bitmap_create(w, h, format, &result);
            if (st != Ok) {
                hr = st == OutOfMemory ? FX_E_OUTOFMEMORY : FX_E_FAIL;
            } else if (format == PixelFormat32bppARGB) {
                hr = effect_execute(e, &rect, (UINT32 *)result->bits, result->stride / 4);
            } else {
                UINT32 *scratch = gp_alloc(sizeof(UINT32) * (SIZE_T)w * (SIZE_T)h);
                if (!scratch) {
                    hr = FX_E_OUTOFMEMORY;
                } else {
                    hr = effect_execute(e, &rect, scratch, w);
                    for (INT y = 0; y < h && hr == FX_OK; y++)
                        gp_bitmap_write_argb(result, 0, y, w, scratch + (SSIZE_T)y * w);
                    gp_free(scratch);
                }
            }
        }
        if (hr == FX_OK && use_aux && aux && aux_size)
            hr = aux_data(e, aux, aux_size);
    }
    e->initialized = FALSE;
    if (copied)
        gp_free(image.pixels);
    if (hr != FX_OK) {
        gp_bitmap_free(result);
        return status_of(hr);
    }
    if (output_rect)
        *output_rect = rect;
    *output = result;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBitmapCreateApplyEffect(GpBitmap **inputs, INT count, CGpEffect *effect, RECT *roi,
                                                       RECT *output_rect, GpBitmap **output, BOOL use_aux, VOID **aux,
                                                       INT *aux_size)
{
    GEffect *e = effect_cast(effect);
    if (!output || !e || count < 0)
        return InvalidParameter;
    /* every effect takes exactly one image. */
    if (count != 1 || !inputs)
        return InvalidParameter;
    GBitmap *b = gp_bitmap_cast(inputs[0]);
    if (!b)
        return InvalidParameter;
    GBitmap *result;
    GpStatus st = create_applied(e, b, roi, &result, output_rect, use_aux, aux, aux_size);
    if (st == Ok)
        *output = result;
    return st;
}

/* ------------------------------------------------------------------ drawing */

/* the whole pixels that hold a rectangle of reals, one more than they need to on the right and bottom. */
static RECT bounds_to_rect(const GpRectF *r)
{
    RECT out = {0, 0, 0, 0};
    const float limit = 1073741800.0f;
    if (r->X < -limit || r->X > limit || r->Y < -limit || r->Y > limit || r->Width < 0.0f || r->Width > limit ||
        r->Height < 0.0f || r->Height > limit)
        return out;
    INT x = fx_float_to_int(mime_vec_floorf(r->X)), y = fx_float_to_int(mime_vec_floorf(r->Y));
    INT right = fx_float_to_int(mime_vec_ceilf(r->Width + r->X)), bottom = fx_float_to_int(mime_vec_ceilf(r->Height + r->Y));
    out.left = x;
    out.top = y;
    out.right = x + (right - x + 1);
    out.bottom = y + (bottom - y + 1);
    return out;
}

static float unit_to_pixels(GpUnit unit, float dpi)
{
    switch (unit) {
    case UnitPoint:
        return dpi / 72.0f;
    case UnitDocument:
        return dpi / 300.0f;
    case UnitMillimeter:
        return dpi / 25.4f;
    default:
        return dpi;
    }
}

/*
 * draws a part of the image through a matrix after the effect has been applied to it. the matrix takes the
 * rectangle to the page, the units of the rectangle are turned into pixels first and a negative size flips.
 */
WINGDIPAPI GpStatus WINAPI GdipDrawImageFX(GpGraphics *graphics, GpImage *image, GpRectF *source, GpMatrix *xform,
                                           CGpEffect *effect, GpImageAttributes *attrs, GpUnit unit)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GImage *img = gp_image_cast(image);
    if (!g || !img || (UINT)unit - UnitPixel > 4)
        return InvalidParameter;
    GEffect *e = effect ? effect_cast(effect) : nullptr;
    GMatrix *gm = xform ? gp_matrix_cast(xform) : nullptr;
    struct GAttrs *a = attrs ? gp_attrs_cast(attrs) : nullptr;
    if ((effect && !e) || (xform && !gm) || (attrs && !a))
        return InvalidParameter;
    GBitmap *b = gp_bitmap_cast(img);
    if (b && b->locked)
        return WrongState;

    MimeVecMatrix m;
    mime_vec_matrix_identity(&m);
    if (gm)
        m = gm->m;
    GpRectF r;
    if (!source) {
        GpUnit bounds_unit;
        if (b) {
            r = (GpRectF){0.0f, 0.0f, (float)b->width, (float)b->height};
        } else {
            GpStatus st = gp_metafile_bounds(img, &r, &bounds_unit);
            if (st != Ok)
                return st;
        }
    } else {
        r = *source;
        if (unit != UnitPixel) {
            float dpi_x = 96.0f, dpi_y = 96.0f;
            if (b) {
                dpi_x = b->dpi_x;
                dpi_y = b->dpi_y;
            } else {
                gp_metafile_resolution(img, &dpi_x, &dpi_y);
            }
            float fx = unit_to_pixels(unit, dpi_x), fy = unit_to_pixels(unit, dpi_y);
            r.X *= fx;
            r.Width *= fx;
            r.Y *= fy;
            r.Height *= fy;
            mime_vec_matrix_scale(&m, 1.0f / fx, 1.0f / fy);
        }
    }
    if (r.Width < 0.0f) {
        mime_vec_matrix_translate(&m, -r.Width, 0.0f);
        mime_vec_matrix_scale(&m, -1.0f, 1.0f);
        r.X += r.Width;
        r.Width = -r.Width;
    }
    if (r.Height < 0.0f) {
        mime_vec_matrix_translate(&m, 0.0f, -r.Height);
        mime_vec_matrix_scale(&m, 1.0f, -1.0f);
        r.Y += r.Height;
        r.Height = -r.Height;
    }

    GImage *drawn = img;
    GBitmap *applied = nullptr;
    if (e && b) {
        RECT roi = bounds_to_rect(&r), out_rect;
        GpStatus st = create_applied(e, b, &roi, &applied, &out_rect, FALSE, nullptr, nullptr);
        if (st != Ok)
            return st;
        if (!applied)
            return Ok;
        r.X -= (float)out_rect.left;
        r.Y -= (float)out_rect.top;
        mime_vec_matrix_translate(&m, (float)out_rect.left, (float)out_rect.top);
        drawn = &applied->image;
    }
    GpPointF corners[3];
    MimeVecPoint p0 = mime_vec_xform(&m, r.X, r.Y), p1 = mime_vec_xform(&m, r.X + r.Width, r.Y),
                 p2 = mime_vec_xform(&m, r.X, r.Y + r.Height);
    corners[0] = (GpPointF){p0.x, p0.y};
    corners[1] = (GpPointF){p1.x, p1.y};
    corners[2] = (GpPointF){p2.x, p2.y};
    GpStatus st = gp_draw_image(g, drawn, corners, 3, r.X, r.Y, r.Width, r.Height, UnitPixel, a);
    gp_bitmap_free(applied);
    return st;
}

/* ------------------------------------------------------------------ histograms */

WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogramSize(HistogramFormat format, UINT *entries)
{
    if (!entries)
        return InvalidParameter;
    *entries = 256;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogram(GpBitmap *bitmap, HistogramFormat format, UINT entries,
                                                  UINT *ch0, UINT *ch1, UINT *ch2, UINT *ch3)
{
    GBitmap *b = gp_bitmap_cast(bitmap);
    if (!b || entries != 256)
        return InvalidParameter;
    /* the channels a format fills: the first two take all four, rgb three, the rest one. */
    UINT *alpha = nullptr, *red = nullptr, *green = nullptr, *blue = nullptr;
    switch (format) {
    case HistogramFormatARGB:
    case HistogramFormatPARGB:
        if (!ch0 || !ch1 || !ch2 || !ch3)
            return InvalidParameter;
        alpha = ch0;
        red = ch1;
        green = ch2;
        blue = ch3;
        break;
    case HistogramFormatRGB:
        if (!ch0 || !ch1 || !ch2 || ch3)
            return InvalidParameter;
        red = ch0;
        green = ch1;
        blue = ch2;
        break;
    case HistogramFormatGray:
    case HistogramFormatB:
    case HistogramFormatG:
    case HistogramFormatR:
    case HistogramFormatA:
        if (!ch0 || ch1 || ch2 || ch3)
            return InvalidParameter;
        if (format == HistogramFormatB)
            blue = ch0;
        else if (format == HistogramFormatG)
            green = ch0;
        else if (format == HistogramFormatR)
            red = ch0;
        else if (format == HistogramFormatA)
            alpha = ch0;
        break;
    default:
        return InvalidParameter;
    }
    if (b->locked)
        return GenericError;
    UINT *outputs[4] = {blue, green, red, alpha};
    for (INT i = 0; i < 4; i++)
        if (outputs[i])
            memset(outputs[i], 0, 256 * sizeof(UINT));
    if (format == HistogramFormatGray)
        memset(ch0, 0, 256 * sizeof(UINT));
    UINT32 *row = gp_alloc(sizeof(UINT32) * (SIZE_T)b->width);
    if (!row)
        return OutOfMemory;
    for (INT y = 0; y < b->height; y++) {
        gp_bitmap_read_argb(b, 0, y, b->width, row);
        for (INT x = 0; x < b->width; x++) {
            UINT32 c = row[x];
            if (format == HistogramFormatPARGB)
                c = gp_premultiply(c);
            if (format == HistogramFormatGray) {
                float luma = (((float)((c >> 8) & 255) * 0.71516001f + (float)((c >> 16) & 255) * 0.212671f) +
                              (float)(c & 255) * 0.072168998f) +
                             0.5f;
                ch0[(BYTE)fx_float_to_int(mime_vec_floorf(luma))]++;
                continue;
            }
            for (INT i = 0; i < 4; i++)
                if (outputs[i])
                    outputs[i][(c >> (8 * i)) & 255]++;
        }
    }
    gp_free(row);
    return Ok;
}
