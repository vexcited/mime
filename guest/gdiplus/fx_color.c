/* per-pixel effects: the color matrix, lookup tables and the effects built on them, and the tint. */
#include "fx_core.h"

/* ------------------------------------------------------------------ tables */

void fx_identity_tables(BYTE *tables)
{
    for (INT channel = 0; channel < 4; channel++)
        for (INT i = 0; i < 256; i++)
            tables[channel * 256 + i] = (BYTE)i;
}

void fx_apply_tables(const BYTE *tables, const FxJob *job)
{
    const BYTE *blue = tables, *green = tables + 256, *red = tables + 512, *alpha = tables + 768;
    INT width = job->area.right - job->area.left;
    for (INT y = job->area.top; y < job->area.bottom; y++) {
        const UINT32 *in = fx_src_row(job, y);
        UINT32 *out = fx_dst_row(job, y);
        for (INT x = 0; x < width; x++) {
            UINT32 p = in[x];
            out[x] = (UINT32)alpha[p >> 24] << 24 | (UINT32)red[(p >> 16) & 255] << 16 |
                     (UINT32)green[(p >> 8) & 255] << 8 | blue[p & 255];
        }
    }
}

/* ------------------------------------------------------------------ lightness, saturation and hue */

void fx_rgb_to_lsh(INT r, INT g, INT b, INT *l, INT *s, INT *h)
{
    INT hi = fx_max(r, fx_max(g, b)), lo = fx_min(r, fx_min(g, b));
    *l = (hi + lo) >> 1;
    if (hi == lo) {
        *s = 0;
        *h = 255;
        return;
    }
    INT spread = hi - lo;
    INT room = *l >= 128 ? 510 - (hi + lo) : hi + lo;
    *s = 255 * spread / room;
    /* position on the hue circle from red, in units of the spread. */
    INT around;
    if (r == hi)
        around = g - b;
    else if (g == hi)
        around = b + 2 * spread - r;
    else
        around = r + 4 * spread - g;
    INT hue = 43 * around / spread;
    if (hue < 0)
        hue += 255;
    if (hue > 128)
        hue -= 2;
    *h = hue;
}

/* a point within a sextant: low plus left 43rds of the span. */
static INT along(INT low, INT span, INT left) { return low + span * left / 43; }

void fx_lsh_to_rgb(INT l, INT s, INT h, INT *r, INT *g, INT *b)
{
    if (s == 0) {
        *r = *g = *b = l;
        return;
    }
    INT t = s * l / 255;
    if (l >= 128)
        t = s - t;
    INT upper = l + t;
    INT top = (BYTE)upper, low = 2 * l - upper, span = upper - low;
    if (h <= 43) {
        *r = top;
        *g = along(low, span, h);
        *b = low;
    } else if (h <= 85) {
        *r = along(low, span, 85 - h);
        *g = top;
        *b = low;
    } else if (h <= 128) {
        *r = low;
        *g = top;
        *b = along(low, span, h - 85);
    } else if (h <= 170) {
        *r = low;
        *g = along(low, span, 170 - h);
        *b = top;
    } else if (h <= 213) {
        *r = along(low, span, h - 170);
        *g = low;
        *b = top;
    } else {
        *r = top;
        *g = low;
        *b = along(low, span, 255 - h);
    }
    *r = (BYTE)*r;
    *g = (BYTE)*g;
    *b = (BYTE)*b;
}

/* ------------------------------------------------------------------ midtone */

float fx_midtone_gamma(INT midtone)
{
    if (midtone < -100 || midtone > 100)
        return 1.0f;
    /* up to the limit the exponent is slope * a + offset, the pieces meet at the limits. */
    static const struct {
        float limit, slope, offset;
    } piece[] = {{0.5f, 2.0f, 1.0f},       {0.75f, 4.0f, 0.0f},       {0.875f, 8.0f, -3.0f},
                 {0.9375f, 16.0f, -10.0f}, {0.96875f, 32.0f, -25.0f}, {0.984375f, 64.0f, -56.0f},
                 {0.9921875f, 128.0f, -119.0f}};
    float a = (float)(midtone < 0 ? -midtone : midtone) / 100.0f;
    float g = 10.0f;
    for (UINT i = 0; i < sizeof(piece) / sizeof(piece[0]); i++) {
        if (a < piece[i].limit) {
            g = piece[i].slope * a + piece[i].offset;
            break;
        }
    }
    return midtone >= 0 ? 1.0f / g : g;
}

/* ------------------------------------------------------------------ integer parameters */

typedef struct IntRange {
    INT lo, hi;
} IntRange;

/* state of the effects whose parameters are a few ranged integers, plus optional tables. */
typedef struct IntState {
    const IntRange *range;
    UINT count;
    INT v[3];
    BYTE tables[1024];
} IntState;

static UINT int_size(const void *state) { return ((const IntState *)state)->count * sizeof(INT); }

/* values are checked and stored in order, so the ones before a bad one are kept. */
static GpStatus int_set(void *state, const void *params, UINT size)
{
    IntState *s = state;
    if (size != s->count * sizeof(INT))
        return InvalidParameter;
    const INT *in = params;
    for (UINT i = 0; i < s->count; i++) {
        if (in[i] < s->range[i].lo || in[i] > s->range[i].hi)
            return InvalidParameter;
        s->v[i] = in[i];
    }
    return Ok;
}

static void int_get(const void *state, void *out)
{
    const IntState *s = state;
    memcpy(out, s->v, s->count * sizeof(INT));
}

/* ------------------------------------------------------------------ color matrix */

static void matrix_init(void *state)
{
    ColorMatrix *m = state;
    memset(m, 0, sizeof(*m));
    for (INT i = 0; i < 5; i++)
        m->m[i][i] = 1.0f;
}

static UINT matrix_size(const void *state) { return sizeof(ColorMatrix); }

static GpStatus matrix_set(void *state, const void *params, UINT size)
{
    if (size != sizeof(ColorMatrix))
        return InvalidParameter;
    memcpy(state, params, size);
    return Ok;
}

static void matrix_get(const void *state, void *out) { memcpy(out, state, sizeof(ColorMatrix)); }

static GpStatus matrix_run(void *state, const FxJob *job)
{
    const ColorMatrix *m = state;
    INT width = job->area.right - job->area.left;
    for (INT y = job->area.top; y < job->area.bottom; y++) {
        const UINT32 *in = fx_src_row(job, y);
        UINT32 *out = fx_dst_row(job, y);
        for (INT x = 0; x < width; x++) {
            UINT32 p = in[x];
            float c[4] = {(float)((p >> 16) & 255), (float)((p >> 8) & 255), (float)(p & 255), (float)(p >> 24)};
            BYTE n[4];
            for (INT j = 0; j < 4; j++) {
                float sum = c[0] * m->m[0][j] + c[1] * m->m[1][j];
                sum = sum + c[2] * m->m[2][j];
                sum = sum + c[3] * m->m[3][j];
                sum = sum + 255.0f * m->m[4][j];
                n[j] = fx_round_byte(sum);
            }
            out[x] = (UINT32)n[3] << 24 | (UINT32)n[0] << 16 | (UINT32)n[1] << 8 | n[2];
        }
    }
    return Ok;
}

const FxClass fx_class_matrix = {
    &ColorMatrixEffectGuid, sizeof(ColorMatrix), FX_REPORTS_SIZE, matrix_init, nullptr, matrix_size, matrix_set,
    matrix_get, nullptr, matrix_run, nullptr,
};

/* ------------------------------------------------------------------ program lookup tables */

static void lookup_init(void *state) { fx_identity_tables(state); }
static UINT lookup_size(const void *state) { return 1024; }

static GpStatus lookup_set(void *state, const void *params, UINT size)
{
    if (size != 1024)
        return InvalidParameter;
    memcpy(state, params, 1024);
    return Ok;
}

static void lookup_get(const void *state, void *out) { memcpy(out, state, 1024); }

static GpStatus lookup_run(void *state, const FxJob *job)
{
    fx_apply_tables(state, job);
    return Ok;
}

static const BYTE *lookup_tables(void *state) { return state; }

const FxClass fx_class_lookup = {
    &ColorLUTEffectGuid, 1024, FX_KEEPS_24 | FX_REPORTS_SIZE, lookup_init, nullptr, lookup_size, lookup_set,
    lookup_get, nullptr, lookup_run, lookup_tables,
};

/* ------------------------------------------------------------------ table effects */

static GpStatus table_run(void *state, const FxJob *job)
{
    IntState *s = state;
    /* the class rebuilds the tables from the parameters before every run. */
    fx_apply_tables(s->tables, job);
    return Ok;
}

/* brightness and contrast */

static const IntRange brightness_range[2] = {{-255, 255}, {-100, 100}};

static void brightness_init(void *state)
{
    IntState *s = state;
    memset(s, 0, sizeof(*s));
    s->range = brightness_range;
    s->count = 2;
    fx_identity_tables(s->tables);
}

/* a contrast of 100 percent is a straight line up, one step short of vertical. */
static const float contrast_limit = 1.0f - 1.0e-6f;

static const BYTE *brightness_tables(void *state)
{
    IntState *s = state;
    /* with both values 0 the tables are left as they were, not reset to identity. */
    if (s->v[0] == 0 && s->v[1] == 0)
        return s->tables;
    float contrast = (float)s->v[1] / 100.0f;
    float level = (float)(s->v[0] + 255) / 510.0f;
    if (contrast >= 1.0f)
        contrast = contrast_limit;
    float rest = 1.0f - level;
    float gain = contrast + 1.0f;
    float shift, steep = 1.0f - contrast;
    if (contrast > 0.0f)
        shift = (rest - steep * level) * 255.0f;
    else
        shift = (level - rest * gain) * 255.0f;
    for (INT i = 0; i < 256; i++) {
        float v = contrast > 0.0f ? (((float)i - shift) / steep) : ((float)i * gain + shift);
        s->tables[i] = s->tables[256 + i] = s->tables[512 + i] = fx_round_byte(v);
    }
    return s->tables;
}

static GpStatus brightness_run(void *state, const FxJob *job)
{
    brightness_tables(state);
    return table_run(state, job);
}

const FxClass fx_class_brightness = {
    &BrightnessContrastEffectGuid, sizeof(IntState), FX_KEEPS_24, brightness_init, nullptr, int_size, int_set, int_get,
    nullptr, brightness_run, brightness_tables,
};

/* levels */

static const IntRange levels_range[3] = {{0, 100}, {-100, 100}, {0, 100}};

static void levels_init(void *state)
{
    IntState *s = state;
    memset(s, 0, sizeof(*s));
    s->range = levels_range;
    s->count = 3;
    s->v[0] = 100;
    fx_identity_tables(s->tables);
}

static const BYTE *levels_tables(void *state)
{
    IntState *s = state;
    float high = (float)s->v[0] * 255.0f / 100.0f, shadow = (float)s->v[2] * 255.0f / 100.0f;
    float gamma = fx_midtone_gamma(s->v[1]);
    /* the ramp runs from low to top. its value is the base minus the curve, so it falls when the base is 255. */
    float base = 0.0f, low, top;
    if (high > shadow) {
        low = shadow;
        top = high;
    } else if (shadow > high) {
        base = 255.0f;
        low = high;
        top = shadow;
    } else {
        top = high;
        low = high > 127.0f ? shadow + 1.0f : shadow - 1.0f;
    }
    if (low > top) {
        float t = low;
        low = top;
        top = t;
    }
    BYTE fill = (BYTE)fx_floor_int(base + 0.5f);
    /* below the low point the table holds the fill, above the top point the other end. */
    INT flat = low >= 0.0f ? fx_floor_int(low) : -1;
    float width = 1.0f / (top - low);
    for (INT i = 0; i < 256; i++) {
        BYTE v;
        if (i <= flat) {
            v = fill;
        } else if (top > (float)i) {
            float x = ((float)i - low) * width;
            float curve = gamma == 1.0f ? x : (float)gp_pow(x, gamma);
            float d = base - curve * 255.0f;
            v = fx_round_byte(d < 0.0f ? -d : d);
        } else {
            v = (BYTE)~fill;
        }
        s->tables[i] = s->tables[256 + i] = s->tables[512 + i] = v;
    }
    return s->tables;
}

static GpStatus levels_run(void *state, const FxJob *job)
{
    levels_tables(state);
    return table_run(state, job);
}

const FxClass fx_class_levels = {
    &LevelsEffectGuid, sizeof(IntState), FX_KEEPS_24, levels_init, nullptr, int_size, int_set, int_get, nullptr,
    levels_run, levels_tables,
};

/* color balance */

static const IntRange balance_range[3] = {{-100, 100}, {-100, 100}, {-100, 100}};

static void balance_init(void *state)
{
    IntState *s = state;
    memset(s, 0, sizeof(*s));
    s->range = balance_range;
    s->count = 3;
    fx_identity_tables(s->tables);
}

static const BYTE *balance_tables(void *state)
{
    IntState *s = state;
    /* the parameters are red, green and blue, the tables blue, green and red. */
    for (INT channel = 0; channel < 3; channel++) {
        float gain = (float)s->v[2 - channel] / 100.0f + 1.0f;
        for (INT i = 0; i < 256; i++) {
            float v = (float)i * gain;
            s->tables[channel * 256 + i] = v >= 255.0f ? 255 : fx_round_byte(v);
        }
    }
    return s->tables;
}

static GpStatus balance_run(void *state, const FxJob *job)
{
    balance_tables(state);
    return table_run(state, job);
}

const FxClass fx_class_balance = {
    &ColorBalanceEffectGuid, sizeof(IntState), FX_KEEPS_24, balance_init, nullptr, int_size, int_set, int_get,
    nullptr, balance_run, balance_tables,
};

/* hue, saturation and lightness */

static const IntRange hsl_range[3] = {{-180, 180}, {-100, 100}, {-100, 100}};

static void hsl_init(void *state)
{
    IntState *s = state;
    memset(s, 0, sizeof(*s));
    s->range = hsl_range;
    s->count = 3;
    fx_identity_tables(s->tables);
}

/* the lightness, saturation and hue tables sit in the blue, green and red slots. */
static const BYTE *hsl_tables(void *state)
{
    IntState *s = state;
    float lift = (float)s->v[2] / 100.0f * 255.0f;
    float gain = (float)s->v[1] / 100.0f + 1.0f;
    float hue = (float)(s->v[0] < 0 ? s->v[0] + 360 : s->v[0]);
    float turn = hue * 255.0f / 360.0f;
    for (INT i = 0; i < 256; i++) {
        s->tables[i] = fx_round_byte(lift + (float)i);
        s->tables[256 + i] = fx_round_byte((float)i * gain);
        float x = (float)i + turn;
        if (x <= 255.0f) {
            if (x < 0.0f)
                x += 255.0f;
        } else {
            x -= 255.0f;
        }
        s->tables[512 + i] = (BYTE)fx_floor_int(x + 0.5f);
    }
    return s->tables;
}

static GpStatus hsl_run(void *state, const FxJob *job)
{
    IntState *s = state;
    hsl_tables(s);
    const BYTE *light = s->tables, *saturation = s->tables + 256, *hue = s->tables + 512;
    INT width = job->area.right - job->area.left;
    for (INT y = job->area.top; y < job->area.bottom; y++) {
        const UINT32 *in = fx_src_row(job, y);
        UINT32 *out = fx_dst_row(job, y);
        for (INT x = 0; x < width; x++) {
            UINT32 p = in[x];
            INT l, sat, h, r, g, b;
            fx_rgb_to_lsh((p >> 16) & 255, (p >> 8) & 255, p & 255, &l, &sat, &h);
            fx_lsh_to_rgb(light[l], saturation[sat], hue[h], &r, &g, &b);
            out[x] = (p & 0xff000000u) | (UINT32)r << 16 | (UINT32)g << 8 | (UINT32)b;
        }
    }
    return Ok;
}

const FxClass fx_class_hsl = {
    &HueSaturationLightnessEffectGuid, sizeof(IntState), FX_KEEPS_24, hsl_init, nullptr, int_size, int_set, int_get,
    nullptr, hsl_run, hsl_tables,
};

/* ------------------------------------------------------------------ tint */

static const IntRange tint_range[2] = {{-180, 180}, {-100, 100}};

static void tint_init(void *state)
{
    IntState *s = state;
    memset(s, 0, sizeof(*s));
    s->range = tint_range;
    s->count = 2;
}

static GpStatus tint_run(void *state, const FxJob *job)
{
    const IntState *s = state;
    /* the tint color has the hue as a byte, half the lightness and all the saturation. */
    INT hue_byte = (BYTE)fx_floor_int((float)s->v[0] * 255.0f / 360.0f + 0.5f);
    INT tr, tg, tb;
    fx_lsh_to_rgb(127, 255, hue_byte, &tr, &tg, &tb);
    INT amount = fx_floor_int((float)s->v[1] * 2.55f + 0.5f), keep = 255 - amount;
    INT width = job->area.right - job->area.left;
    for (INT y = job->area.top; y < job->area.bottom; y++) {
        const UINT32 *in = fx_src_row(job, y);
        UINT32 *out = fx_dst_row(job, y);
        for (INT x = 0; x < width; x++) {
            UINT32 p = in[x];
            INT r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            INT pull = (amount * fx_max(r, fx_max(g, b))) >> 8;
            INT mr = tr * pull + keep * r, mg = tg * pull + keep * g, mb = tb * pull + keep * b;
            /* give back the brightness the mix took from the pixel. */
            INT back = 19 * b + 54 * r + 183 * g - 54 * (mr >> 8) - 183 * (mg >> 8) - 19 * (mb >> 8);
            out[x] = (p & 0xff000000u) | (UINT32)fx_byte((back + mr) >> 8) << 16 |
                     (UINT32)fx_byte((back + mg) >> 8) << 8 | fx_byte((back + mb) >> 8);
        }
    }
    return Ok;
}

const FxClass fx_class_tint = {
    &TintEffectGuid, sizeof(IntState), FX_KEEPS_24, tint_init, nullptr, int_size, int_set, int_get, nullptr, tint_run,
    nullptr,
};
