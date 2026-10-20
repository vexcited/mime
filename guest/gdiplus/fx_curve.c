/*
 * the color curves. each color channel is a curve through 23 control points on a regular grid, the adjustments move
 * the points and the lookup table is a spline through them. the points a channel had before the last adjustment are
 * kept, so the same adjustment made again starts from there.
 */
#include "fx_core.h"

#define POINTS 23
#define PI 3.14159265358979323846

/* the adjustments, as the sdk numbers them. */
enum { ADJ_EXPOSURE, ADJ_DENSITY, ADJ_CONTRAST, ADJ_HIGHLIGHT, ADJ_SHADOW, ADJ_MIDTONE, ADJ_WHITE, ADJ_BLACK };

/* the bumps the shaping adjustments use. */
enum { BUMP_SHADOW, BUMP_CONTRAST, BUMP_HIGHLIGHT, BUMP_COUNT };

typedef struct CurveLine {
    float x[POINTS], y[POINTS];
    float saved_x[POINTS], saved_y[POINTS];
    float fitted[256];
    float white, black;
} CurveLine;

typedef struct CurveState {
    CurveLine line[3];  /* red, green and blue */
    float bump[BUMP_COUNT][POINTS];
    BYTE tables[1024];
    /* what was set last, which is also what the parameters read as. */
    INT adjustment, channel, value;
} CurveState;

/* ------------------------------------------------------------------ the bumps */

static double sine(double x)
{
    while (x > PI)
        x -= 2.0 * PI;
    while (x < -PI)
        x += 2.0 * PI;
    double square = x * x, term = x, sum = x;
    for (INT n = 1; n < 14; n++) {
        term *= -square / ((double)(2 * n) * (double)(2 * n + 1));
        sum += term;
    }
    return sum;
}

static void bumps_make(float bump[BUMP_COUNT][POINTS])
{
    memset(bump, 0, sizeof(float) * BUMP_COUNT * POINTS);
    /* contrast: a minus sine over one period. */
    float angle = 0.0f, step = (float)(2.0 * PI / 22.0);
    for (INT i = 0; i < POINTS; i++) {
        bump[BUMP_CONTRAST][i] = (float)-sine((double)angle);
        angle += step;
    }
    /* highlight: a peak at point 17 that falls away like a gaussian below it and like a sine above. */
    for (INT k = 0; k < 6; k++)
        bump[BUMP_HIGHLIGHT][17 - k] = (float)gp_exp(-(double)(k * k) * PI / (5.5 * 5.5));
    for (INT j = 0; j < 5; j++)
        bump[BUMP_HIGHLIGHT][18 + j] = (float)sine((6 + j) * PI / 10.0);
    /* shadow: a dip that rises as a sine up to point 5 and fades as a gaussian after it. */
    for (INT j = 0; j < 6; j++)
        bump[BUMP_SHADOW][j] = (float)-sine(j * PI / 10.0);
    for (INT j = 0; j < 5; j++)
        bump[BUMP_SHADOW][6 + j] = (float)-gp_exp(-(double)(j * j) * PI / 30.25);
}

/* ------------------------------------------------------------------ the spline */

static void line_reset(CurveLine *c)
{
    float at = 0.0f, step = 255.0f / 22.0f;
    for (INT i = 0; i < POINTS; i++) {
        c->x[i] = c->y[i] = c->saved_x[i] = c->saved_y[i] = at;
        at += step;
    }
    for (INT i = 0; i < 256; i++)
        c->fitted[i] = (float)i;
    c->white = 255.0f;
    c->black = 0.0f;
}

static void line_save(CurveLine *c)
{
    memcpy(c->saved_x, c->x, sizeof(c->x));
    memcpy(c->saved_y, c->y, sizeof(c->y));
}

static void line_restore(CurveLine *c)
{
    memcpy(c->x, c->saved_x, sizeof(c->x));
    memcpy(c->y, c->saved_y, sizeof(c->y));
}

/* the table of the natural cubic spline through the points, at the 256 inputs, and never going down. */
static void line_fit(CurveLine *c)
{
    float h = c->x[1] - c->x[0];
    /* the second derivatives m, from the tridiagonal system of the interior points. */
    float m[POINTS], rhs[POINTS - 2], diag[POINTS - 2];
    for (INT i = 0; i < POINTS - 2; i++) {
        rhs[i] = ((c->y[i + 2] - c->y[i + 1]) - c->y[i + 1] + c->y[i]) * 6.0f / h;
        diag[i] = 4.0f * h;
    }
    for (INT i = 0; i < POINTS - 3; i++) {
        float ratio = h * rhs[i] / diag[i];
        diag[i + 1] -= (h * h) / diag[i];
        rhs[i + 1] -= ratio;
    }
    m[0] = m[POINTS - 1] = 0.0f;
    m[POINTS - 2] = rhs[POINTS - 3] / diag[POINTS - 3];
    for (INT i = POINTS - 4; i >= 0; i--)
        m[i + 1] = (rhs[i] - h * m[i + 2]) / diag[i];

    INT seg = 1;
    float previous = 0.0f;
    for (INT t = 0; t < 256; t++) {
        float at = (float)t;
        while (at >= c->x[seg] && seg < POINTS - 1)
            seg++;
        float x0 = c->x[seg - 1];
        float a = (at - x0) / h, b = ((x0 + h) - at) / h;
        float v = (a * c->y[seg] + b * c->y[seg - 1]) -
                  (((a - a * a * a) * m[seg] + (b - b * b * b) * m[seg - 1]) * (h * h / 6.0f));
        if (v < 0.0f)
            v = 0.0f;
        if (v > 255.0f)
            v = 255.0f;
        if (t > 0 && previous > v)
            v = previous;
        c->fitted[t] = previous = v;
    }
}

/* ------------------------------------------------------------------ the adjustments */

static float clamp_level(float v) { return v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v; }

static void line_density(CurveLine *c, float amount)
{
    for (INT i = 0; i < POINTS; i++)
        c->y[i] = clamp_level(amount + c->y[i]);
}

/* the value of each point becomes the one the old curve had amount further on. */
static void line_exposure(CurveLine *c, float amount)
{
    for (INT i = 0; i < POINTS; i++) {
        INT at = fx_floor_int((c->x[i] - amount) + 0.5f);
        c->y[i] = c->fitted[at < 0 ? 0 : at > 255 ? 255 : at];
    }
}

static void line_midtone(CurveLine *c, float gamma)
{
    for (INT i = 0; i < POINTS; i++) {
        float p = (float)gp_pow((double)(c->y[i] * (1.0f / 255.0f)), (double)gamma) * 255.0f;
        if (p > 255.0f)
            p = 255.0f;
        c->y[i] = p;
        /* the points keep rising. */
        if (i > 0 && c->y[i - 1] >= p)
            c->y[i] = c->y[i - 1] + 0.01f;
    }
}

/* pushes the curve away from the diagonal along the other diagonal by the bump, then reads it back on the grid. */
static void line_shape(CurveLine *c, const float *bump, float strength)
{
    float at[POINTS], to[POINTS];
    BOOL rising = c->y[POINTS - 1] > c->y[0];
    for (INT i = 0; i < POINTS; i++) {
        float push = strength * bump[i];
        float mid = (c->y[i] + c->x[i]) / 2.0f;
        if (rising) {
            float half = push + (c->y[i] - c->x[i]) / 2.0f;
            to[i] = half + mid;
            at[i] = mid - half;
        } else {
            float half = (c->x[i] - c->y[i]) / 2.0f;
            mid -= push;
            at[i] = mid + half;
            to[i] = mid - half;
        }
        /* a fold in the curve is left alone. */
        if (i > 0 && at[i] - at[i - 1] < 0.0f)
            return;
    }
    for (INT i = 0; i < POINTS; i++)
        to[i] = clamp_level(to[i]);
    INT seg = 1;
    for (INT j = 1; j < POINTS; j++) {
        float grid = c->x[j];
        while (grid > at[seg] && seg < POINTS - 1)
            seg++;
        float run = at[seg] - grid, rise = to[seg] - to[seg - 1], width = at[seg] - at[seg - 1];
        if (run < 0.001f || rise < 0.001f || !(width > 0.0f))
            c->y[j] = to[seg];
        else
            c->y[j] = to[seg] - (run / width) * rise;
    }
    /* the middle point is the pivot. */
    for (INT i = 0; i < 11; i++)
        if (c->y[i] > c->y[11])
            c->y[i] = c->y[11];
    for (INT i = 12; i < POINTS; i++)
        if (c->y[i] < c->y[11])
            c->y[i] = c->y[11];
}

/* spreads the range from black to white over the whole. */
static void line_stretch(CurveLine *c)
{
    float scale = (float)(255.0 / (double)(float)(c->white - c->black));
    for (INT i = 0; i < POINTS; i++) {
        if (c->y[i] <= c->black)
            c->y[i] = 0.0f;
        else if (c->y[i] >= c->white)
            c->y[i] = 255.0f;
        else
            c->y[i] = (c->y[i] - c->black) * scale;
    }
}

/* ------------------------------------------------------------------ setting */

/* the channel a parameter selects: 0 to 2 is one of red, green and blue, 3 is all of them. */
static INT channel_index(INT channel) { return channel >= 1 && channel <= 3 ? channel - 1 : 3; }

static void adjust(CurveState *s, INT adjustment, INT channel, float amount, const float *bump)
{
    INT which = channel_index(channel);
    if (adjustment == ADJ_EXPOSURE)
        for (INT c = 0; c < 3; c++)
            line_fit(&s->line[c]);
    float whole = (float)fx_floor_int(amount + 0.5f);
    float gamma = adjustment == ADJ_MIDTONE ? fx_midtone_gamma((INT)whole) : 1.0f;
    for (INT c = 0; c < 3; c++) {
        if (which != 3 && which != c)
            continue;
        CurveLine *line = &s->line[c];
        line_save(line);
        switch (adjustment) {
        case ADJ_EXPOSURE:
            line_exposure(line, whole);
            break;
        case ADJ_DENSITY:
            line_density(line, whole);
            break;
        case ADJ_MIDTONE:
            line_midtone(line, gamma);
            break;
        default:
            line_shape(line, bump, amount);
            break;
        }
    }
}

static void restore_all(CurveState *s)
{
    for (INT c = 0; c < 3; c++)
        line_restore(&s->line[c]);
}

/* the black or the white point of one channel or all of them. */
static GpStatus set_boundary(CurveState *s, INT channel, INT value, INT adjustment)
{
    BOOL restore = s->adjustment == ADJ_WHITE || s->adjustment == ADJ_BLACK;
    INT which = channel_index(channel);
    for (INT c = 0; c < 3; c++) {
        if (which != 3 && which != c)
            continue;
        CurveLine *line = &s->line[c];
        if (adjustment == ADJ_WHITE) {
            if ((float)value <= line->black)
                return InvalidParameter;
            line->white = (float)value;
        } else {
            if ((float)value >= line->white)
                return InvalidParameter;
            line->black = (float)value;
        }
        if (restore) {
            restore_all(s);
            restore = FALSE;
        }
        line_save(line);
        line_stretch(line);
    }
    return Ok;
}

static GpStatus apply_value(CurveState *s, INT adjustment, INT channel, INT value)
{
    static const float scale[6] = {-1.0f, 1.0f, 0.41141f, 0.360624f, -0.360624f, 1.0f};
    static const INT bump_of[6] = {-1, -1, BUMP_CONTRAST, BUMP_HIGHLIGHT, BUMP_SHADOW, -1};
    if (adjustment < ADJ_EXPOSURE || adjustment > ADJ_BLACK)
        return InvalidParameter;
    if (adjustment >= ADJ_WHITE) {
        if (value < 0 || value > 255)
            return InvalidParameter;
        /* the white point at the top and the black point at the bottom are where they are. */
        if ((adjustment == ADJ_WHITE && value == 255) || (adjustment == ADJ_BLACK && value == 0))
            return Ok;
        return set_boundary(s, channel, value, adjustment);
    }
    INT limit = adjustment <= ADJ_DENSITY ? 256 : 100;
    if (value < -limit || value > limit)
        return InvalidParameter;
    if (s->adjustment == adjustment)
        restore_all(s);
    if (value != 0)
        adjust(s, adjustment, channel, (float)value * scale[adjustment],
               bump_of[adjustment] >= 0 ? s->bump[bump_of[adjustment]] : nullptr);
    return Ok;
}

/* ------------------------------------------------------------------ the class */

static void curve_init(void *state)
{
    CurveState *s = state;
    bumps_make(s->bump);
    for (INT c = 0; c < 3; c++)
        line_reset(&s->line[c]);
    fx_identity_tables(s->tables);
    s->adjustment = ADJ_CONTRAST;
    s->channel = CurveChannelAll;
    s->value = 0;
}

static UINT curve_size(const void *state) { return sizeof(ColorCurveParams); }

static GpStatus curve_set(void *state, const void *params, UINT size)
{
    CurveState *s = state;
    const ColorCurveParams *p = params;
    if (size != sizeof(ColorCurveParams))
        return InvalidParameter;
    s->channel = p->channel;
    s->value = p->adjustValue;
    GpStatus st = apply_value(s, p->adjustment, p->channel, p->adjustValue);
    /* what was asked is remembered also when it was refused. */
    s->adjustment = p->adjustment;
    return st;
}

static void curve_get(const void *state, void *out)
{
    const CurveState *s = state;
    ColorCurveParams *p = out;
    p->adjustment = (CurveAdjustments)s->adjustment;
    p->channel = (CurveChannel)s->channel;
    p->adjustValue = s->value;
}

static const BYTE *curve_tables(void *state)
{
    CurveState *s = state;
    for (INT c = 0; c < 3; c++) {
        line_fit(&s->line[c]);
        /* red is the first curve and goes to the last of the three color tables. */
        BYTE *table = s->tables + 256 * (2 - c);
        for (INT i = 0; i < 256; i++)
            table[i] = fx_round_byte(s->line[c].fitted[i]);
    }
    return s->tables;
}

static GpStatus curve_run(void *state, const FxJob *job)
{
    fx_apply_tables(curve_tables(state), job);
    return Ok;
}

const FxClass fx_class_curve = {
    &ColorCurveEffectGuid, sizeof(CurveState), FX_KEEPS_24, curve_init, nullptr, curve_size, curve_set, curve_get,
    nullptr, curve_run, curve_tables,
};
