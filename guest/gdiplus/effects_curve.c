/*
 * the color curve effect: a curve of 23 control points per channel, moved by one adjustment after the other, and
 * fitted to a table of 256 values by a natural cubic spline.
 *
 * each adjustment of the sdk is one operation on the control points. an adjustment that is set again right after
 * itself starts again from the points as they were before it, the ones before that stay.
 */
#include "fx_private.h"

#define POINTS 23
#define CHANNELS 3

struct FxCurve {
    INT adjustment, channel, value;
    INT white[CHANNELS], black[CHANNELS];
    /* the control points and what they were before the last adjustment. channels are red, green, blue. */
    float x[CHANNELS][POINTS], y[CHANNELS][POINTS];
    float saved_x[CHANNELS][POINTS], saved_y[CHANNELS][POINTS];
    /* the fitted curve of each channel */
    float fitted[CHANNELS][256];
    /* shadow, contrast and highlight: how far the adjustments move each control point */
    float bump[CHANNELS][POINTS];
};

enum {
    ADJUST_EXPOSURE,
    ADJUST_DENSITY,
    ADJUST_CONTRAST,
    ADJUST_HIGHLIGHT,
    ADJUST_SHADOW,
    ADJUST_MIDTONE,
    ADJUST_WHITE_SATURATION,
    ADJUST_BLACK_SATURATION,
};

/* ------------------------------------------------------------------ numbers */

/* the sine of an angle of a few turns, taken as a series after the reduction to a half turn. */
static double sine(double a)
{
    const double pi = 3.141592653589793;
    while (a > pi)
        a -= 2.0 * pi;
    while (a < -pi)
        a += 2.0 * pi;
    double term = a, sum = a, sq = a * a;
    for (INT k = 1; k < 14; k++) {
        term *= -sq / ((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sum;
}

static float smaller(float a, float b) { return a < b ? a : b; }

static float clamp_curve(float v)
{
    if (!(v > 0.0f))
        return 0.0f;
    return v > 255.0f ? 255.0f : v;
}

/* ------------------------------------------------------------------ the bump tables */

static void make_bumps(FxCurve *c)
{
    const float step = 0.28559935f;
    float angle = 0.0f;
    for (INT i = 0; i < POINTS; i++) {
        c->bump[0][i] = 0.0f;
        c->bump[1][i] = (float)-sine((double)angle);
        c->bump[2][i] = 0.0f;
        angle += step;
    }
    const double pi = 3.141592653589793;
    /* highlights: a rise of the exponential to a peak at the 17th point and the half turn of a sine after */
    float rise = 3.14159274f / (5.5f * 5.5f);
    for (INT k = 0; k < 6; k++)
        c->bump[2][17 - k] = (float)gp_exp((double)-((float)(k * k) * rise));
    for (INT j = 0; j < 5; j++)
        c->bump[2][18 + j] = (float)sine((double)(float)(6 + j) * pi / 10.0);
    /* shadows: the sine of a turn to a valley, then the same exponential the other way, below zero */
    for (INT j = 0; j < 6; j++)
        c->bump[0][j] = (float)-sine((double)(float)j * pi / 10.0);
    for (INT j = 0; j < 5; j++)
        c->bump[0][6 + j] = (float)-gp_exp(-((double)(float)(j * j) * pi / 30.25));
}

/* ------------------------------------------------------------------ the control points */

static void reset_points(FxCurve *c, INT ch)
{
    float v = 0.0f;
    for (INT i = 0; i < POINTS; i++) {
        c->x[ch][i] = c->y[ch][i] = c->saved_x[ch][i] = c->saved_y[ch][i] = v;
        v += 11.590909f;
    }
}

static void save_points(FxCurve *c, INT ch)
{
    memcpy(c->saved_x[ch], c->x[ch], sizeof(c->x[ch]));
    memcpy(c->saved_y[ch], c->y[ch], sizeof(c->y[ch]));
}

static void restore_points(FxCurve *c)
{
    memcpy(c->x, c->saved_x, sizeof(c->x));
    memcpy(c->y, c->saved_y, sizeof(c->y));
}

FxCurve *fx_curve_new(void)
{
    FxCurve *c = gp_alloc_zero(sizeof(*c));
    if (!c)
        return nullptr;
    c->adjustment = ADJUST_CONTRAST;
    make_bumps(c);
    for (INT ch = 0; ch < CHANNELS; ch++) {
        c->white[ch] = 255;
        c->black[ch] = 0;
        reset_points(c, ch);
        for (INT i = 0; i < 256; i++)
            c->fitted[ch][i] = (float)i;
    }
    return c;
}

void fx_curve_free(FxCurve *c) { gp_free(c); }

/* ------------------------------------------------------------------ the spline */

/* the second derivatives of the natural cubic spline through the points y, which are h apart. */
static void spline_coefficients(const float *y, float h, float *m)
{
    float d[POINTS - 2], diag[POINTS - 2];
    float before = y[0];
    for (INT i = 0; i < POINTS - 2; i++) {
        float second = ((y[i + 2] - y[i + 1]) - y[i + 1]) + before;
        before = y[i + 1];
        d[i] = (second * 6.0f) / h;
        diag[i] = h * 4.0f;
    }
    for (INT i = 0; i < POINTS - 3; i++) {
        float ratio = (h * d[i]) / diag[i];
        diag[i + 1] = diag[i + 1] - ((h * h) / diag[i]);
        d[i + 1] = d[i + 1] - ratio;
    }
    m[POINTS - 2] = d[POINTS - 3] / diag[POINTS - 3];
    for (INT i = POINTS - 4; i >= 0; i--)
        m[i + 1] = (d[i] - h * m[i + 2]) / diag[i];
    m[0] = 0.0f;
    m[POINTS - 1] = 0.0f;
}

static float spline_value(float at, const float *x, const float *y, const float *m, float h, INT seg)
{
    float x0 = x[seg - 1];
    float a = (at - x0) / h;
    float b = ((x0 + h) - at) / h;
    float v = (a * y[seg] + b * y[seg - 1]) -
              (((a - (a * a) * a) * m[seg] + (b - (b * b) * b) * m[seg - 1]) * ((h * h) / 6.0f));
    return clamp_curve(v);
}

/* the values of the curve at 0 to 255, never going down. */
static void fit_channel(FxCurve *c, INT ch)
{
    float m[POINTS];
    float h = c->x[ch][1] - c->x[ch][0];
    spline_coefficients(c->y[ch], h, m);
    INT seg = 1;
    for (INT i = 0; i < 256; i++) {
        float at = (float)i;
        while (at >= c->x[ch][seg] && seg < POINTS - 1)
            seg++;
        float v = spline_value(at, c->x[ch], c->y[ch], m, h, seg);
        if (i > 0 && c->fitted[ch][i - 1] > v)
            v = c->fitted[ch][i - 1];
        c->fitted[ch][i] = v;
    }
}

/* ------------------------------------------------------------------ the adjustments */

static void density(FxCurve *c, INT ch, INT amount)
{
    for (INT i = 0; i < POINTS; i++) {
        float v = (float)amount + c->y[ch][i];
        c->y[ch][i] = v >= 0.0f ? (v > 255.0f ? 255.0f : v) : 0.0f;
    }
}

static void exposure(FxCurve *c, INT ch, INT amount)
{
    for (INT i = 0; i < POINTS; i++) {
        INT at = fx_float_to_int(mime_vec_floorf((c->x[ch][i] - (float)amount) + 0.5f));
        float v;
        if ((float)at >= 0.0f)
            v = (float)at <= 255.0f ? c->fitted[ch][at] : c->fitted[ch][255];
        else
            v = c->fitted[ch][0];
        c->y[ch][i] = v;
    }
}

static void midtone(FxCurve *c, INT ch, float gamma)
{
    for (INT i = 0; i < POINTS; i++) {
        float v = (float)gp_pow((double)(c->y[ch][i] * 0.0039215689f), (double)gamma);
        float p = smaller(255.0f, v * 255.0f);
        c->y[ch][i] = p;
        if (i > 0 && c->y[ch][i - 1] >= p)
            c->y[ch][i] = c->y[ch][i - 1] + 0.01f;
    }
}

/* moves the points sideways off the diagonal by the strength of the bump at each, and puts the curve back on the grid */
static void shape(FxCurve *c, INT ch, const float *bump, float strength)
{
    float *x = c->x[ch], *y = c->y[ch];
    float at[POINTS], to[POINTS], grid[POINTS];
    BOOL falling = y[POINTS - 1] <= y[0];
    for (INT i = 0; i < POINTS; i++) {
        float px = x[i], py = y[i];
        grid[i] = px;
        if (falling) {
            float half = (px - py) * 0.5f;
            float mid = ((py + px) * 0.5f) - (strength * bump[i]);
            at[i] = mid + half;
            to[i] = mid - half;
        } else {
            float t = strength * bump[i];
            float mid = (py + px) * 0.5f;
            float half = t + ((py - px) * 0.5f);
            to[i] = half + mid;
            at[i] = mid - half;
        }
        if (i > 0 && at[i] - at[i - 1] < 0.0f)
            return;
    }
    for (INT i = 0; i < POINTS; i++)
        to[i] = to[i] >= 0.0f ? (to[i] > 255.0f ? 255.0f : to[i]) : 0.0f;
    INT seg = 1;
    for (INT j = 1; j < POINTS; j++) {
        float want = grid[j];
        x[j] = want;
        while (want > at[seg] && seg < POINTS - 1)
            seg++;
        float run = at[seg] - want;
        float rise = to[seg] - to[seg - 1];
        float v;
        if (run < 0.001f || rise < 0.001f)
            v = to[seg];
        else
            v = to[seg] - (run / (at[seg] - at[seg - 1])) * rise;
        y[j] = v;
    }
    /* the middle point is where the curve is kept in order from */
    for (INT i = 0; i < 11; i++)
        if (y[i] > y[11])
            y[i] = y[11];
    for (INT i = 12; i < POINTS; i++)
        if (y[11] > y[i])
            y[i] = y[11];
}

/* the channel of a request in the order of the tables: red, green, blue, or 3 for all */
static INT channel_index(INT channel)
{
    switch (channel) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 3:
        return 2;
    default:
        return 3;
    }
}

static void adjust(FxCurve *c, INT adjustment, INT channel, float amount, const float *bump)
{
    INT ch = channel_index(channel);
    if (adjustment == ADJUST_EXPOSURE)
        for (INT i = 0; i < CHANNELS; i++)
            fit_channel(c, i);
    INT first = ch == 3 ? 0 : ch, last = ch == 3 ? CHANNELS : ch + 1;
    INT whole = fx_float_to_int(mime_vec_floorf(amount + 0.5f));
    float gamma = adjustment == ADJUST_MIDTONE ? fx_gamma_from_midtone(whole) : 1.0f;
    for (INT i = first; i < last; i++) {
        save_points(c, i);
        switch (adjustment) {
        case ADJUST_EXPOSURE:
            exposure(c, i, whole);
            break;
        case ADJUST_DENSITY:
            density(c, i, whole);
            break;
        case ADJUST_CONTRAST:
        case ADJUST_HIGHLIGHT:
        case ADJUST_SHADOW:
            shape(c, i, bump, amount);
            break;
        default:
            midtone(c, i, gamma);
            break;
        }
    }
}

/* black and white points: the control points are stretched to fill the range between them. */
static void stretch(FxCurve *c, INT ch)
{
    float scale = (float)(255.0 / (double)(float)(c->white[ch] - c->black[ch]));
    float black = (float)c->black[ch], white = (float)c->white[ch];
    for (INT i = 0; i < POINTS; i++) {
        float v = c->y[ch][i], out;
        if (black < v)
            out = v < white ? (v - black) * scale : 255.0f;
        else
            out = 0.0f;
        c->y[ch][i] = out;
    }
}

static FxResult set_boundary(FxCurve *c, INT channel, INT value, INT adjustment)
{
    BOOL restore = c->adjustment == ADJUST_WHITE_SATURATION || c->adjustment == ADJUST_BLACK_SATURATION;
    INT ch = channel_index(channel);
    if (ch == 3) {
        for (INT i = 0; i < CHANNELS; i++) {
            if (adjustment == ADJUST_WHITE_SATURATION) {
                if (value <= c->black[i])
                    return FX_E_INVALIDARG;
                c->white[i] = value;
            } else {
                if (adjustment != ADJUST_BLACK_SATURATION || value >= c->white[i])
                    return FX_E_INVALIDARG;
                c->black[i] = value;
            }
            if (restore) {
                restore_points(c);
                restore = FALSE;
            }
            save_points(c, i);
            stretch(c, i);
        }
        return FX_OK;
    }
    if (adjustment == ADJUST_WHITE_SATURATION && value > c->black[ch]) {
        c->white[ch] = value;
    } else {
        if (adjustment != ADJUST_BLACK_SATURATION || value >= c->white[ch])
            return FX_E_INVALIDARG;
        c->black[ch] = value;
    }
    if (restore)
        restore_points(c);
    save_points(c, ch);
    stretch(c, ch);
    return FX_OK;
}

static FxResult set_value(FxCurve *c, INT low, INT high, INT adjustment, float scale, const float *bump)
{
    if (c->value < low || c->value > high)
        return FX_E_INVALIDARG;
    if (c->adjustment == adjustment)
        restore_points(c);
    if (c->value != 0)
        adjust(c, adjustment, c->channel, (float)c->value * scale, bump);
    return FX_OK;
}

FxResult fx_curve_set(FxCurve *c, const INT *params)
{
    FxResult hr;
    c->channel = params[1];
    c->value = params[2];
    switch (params[0]) {
    case ADJUST_EXPOSURE:
        hr = set_value(c, -256, 256, ADJUST_EXPOSURE, -1.0f, nullptr);
        break;
    case ADJUST_DENSITY:
        hr = set_value(c, -256, 256, ADJUST_DENSITY, 1.0f, nullptr);
        break;
    case ADJUST_CONTRAST:
        hr = set_value(c, -100, 100, ADJUST_CONTRAST, 0.41141f, c->bump[1]);
        break;
    case ADJUST_HIGHLIGHT:
        hr = set_value(c, -100, 100, ADJUST_HIGHLIGHT, 0.360624f, c->bump[2]);
        break;
    case ADJUST_SHADOW:
        hr = set_value(c, -100, 100, ADJUST_SHADOW, -0.360624f, c->bump[0]);
        break;
    case ADJUST_MIDTONE:
        hr = set_value(c, -100, 100, ADJUST_MIDTONE, 1.0f, nullptr);
        break;
    case ADJUST_WHITE_SATURATION:
    case ADJUST_BLACK_SATURATION:
        if (c->value < 0 || c->value > 255) {
            hr = FX_E_INVALIDARG;
        } else if (params[0] == ADJUST_WHITE_SATURATION ? c->value != 255 : c->value != 0) {
            hr = set_boundary(c, c->channel, c->value, params[0]);
        } else {
            hr = FX_OK;
        }
        break;
    default:
        hr = FX_E_INVALIDARG;
        break;
    }
    c->adjustment = params[0];
    return hr;
}

void fx_curve_get(const FxCurve *c, INT *params)
{
    params[0] = c->adjustment;
    params[1] = c->channel;
    params[2] = c->value;
}

void fx_curve_tables(FxCurve *c, BYTE *tables)
{
    for (INT ch = 0; ch < CHANNELS; ch++)
        fit_channel(c, ch);
    for (INT i = 0; i < 256; i++) {
        tables[512 + i] = (BYTE)fx_float_to_int(mime_vec_floorf(c->fitted[0][i] + 0.5f));
        tables[256 + i] = (BYTE)fx_float_to_int(mime_vec_floorf(c->fitted[1][i] + 0.5f));
        tables[i] = (BYTE)fx_float_to_int(mime_vec_floorf(c->fitted[2][i] + 0.5f));
    }
}
