#include "effects.h"

/* ------------------------------------------------------------------ helpers */

/* the straight color of a premultiplied pixel, black where it is transparent, as d2d's shaders divide. */
static inline void unpremul(const float *p, float *c)
{
    if (p[3] == 0.0f) {
        c[0] = c[1] = c[2] = 0.0f;
    } else {
        c[0] = p[0] / p[3];
        c[1] = p[1] / p[3];
        c[2] = p[2] / p[3];
    }
    c[3] = p[3];
}

static inline void premul_out(const float *c, float *p)
{
    p[0] = c[0] * c[3];
    p[1] = c[1] * c[3];
    p[2] = c[2] * c[3];
    p[3] = c[3];
}

static inline float sat(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }
static inline float fracf(float v) { return v - D2dFloorF(v); }

/* a color matrix as the color matrix effect takes it: rows are the input channels and the offset, columns the outputs. */
typedef struct Matrix54 {
    float m[5][4];
    BOOL straight;
    BOOL clamp;
} Matrix54;

static void matrix_identity(Matrix54 *k)
{
    memset(k, 0, sizeof(*k));
    for (int i = 0; i < 4; i++)
        k->m[i][i] = 1.0f;
}

static void matrix_apply(const Matrix54 *k, float *p)
{
    float c[4], o[4];
    if (k->straight)
        memcpy(c, p, sizeof(c));
    else
        unpremul(p, c);
    for (int j = 0; j < 4; j++)
        o[j] = c[0] * k->m[0][j] + c[1] * k->m[1][j] + c[2] * k->m[2][j] + c[3] * k->m[3][j] + k->m[4][j];
    if (k->clamp) {
        for (int j = 0; j < 4; j++)
            o[j] = sat(o[j]);
    }
    if (k->straight)
        memcpy(p, o, sizeof(o));
    else
        premul_out(o, p);
}

/* the matrix the color effects that are color matrices in windows build from their properties. */
static BOOL effect_matrix(D2dEffect *e, Matrix54 *k)
{
    matrix_identity(k);
    float *f = D2dFxF(e, 0);
    switch (e->id) {
    case EFF_COLORMATRIX:
        memcpy(k->m, f, sizeof(k->m));
        k->straight = *D2dFxU(e, 1) == FX_STRAIGHT;
        k->clamp = *D2dFxU(e, 2) != 0;
        return TRUE;
    case EFF_OPACITY:
        k->m[3][3] = f[0];
        return TRUE;
    case EFF_TINT:
        for (int i = 0; i < 4; i++)
            k->m[i][i] = f[i];
        k->clamp = *D2dFxU(e, 1) != 0;
        return TRUE;
    case EFF_SATURATION: {
        float s = f[0];
        k->m[0][0] = 0.213f + 0.787f * s;
        k->m[0][1] = k->m[0][2] = 0.213f - 0.213f * s;
        k->m[1][0] = k->m[1][2] = 0.715f - 0.715f * s;
        k->m[1][1] = 0.715f + 0.285f * s;
        k->m[2][0] = k->m[2][1] = 0.072f - 0.072f * s;
        k->m[2][2] = 0.072f + 0.928f * s;
        return TRUE;
    }
    case EFF_HUEROTATE: {
        float sn, cs;
        D2dSinCosF((float)(f[0] * 3.141592653589793 / 180.0), &sn, &cs);
        k->m[0][0] = 0.213f + cs * 0.787f - sn * 0.213f;
        k->m[0][1] = 0.213f - cs * 0.213f + sn * 0.143f;
        k->m[0][2] = 0.213f - cs * 0.213f - sn * 0.787f;
        k->m[1][0] = 0.715f - cs * 0.715f - sn * 0.715f;
        k->m[1][1] = 0.715f + cs * 0.285f + sn * 0.140f;
        k->m[1][2] = 0.715f - cs * 0.715f + sn * 0.715f;
        k->m[2][0] = 0.072f - cs * 0.072f + sn * 0.928f;
        k->m[2][1] = 0.072f - cs * 0.072f - sn * 0.283f;
        k->m[2][2] = 0.072f + cs * 0.928f + sn * 0.072f;
        return TRUE;
    }
    case EFF_GRAYSCALE:
        for (int j = 0; j < 3; j++) {
            k->m[0][j] = 0.299f;
            k->m[1][j] = 0.587f;
            k->m[2][j] = 0.114f;
        }
        return TRUE;
    case EFF_SEPIA: {
        float t = f[0];
        for (int j = 0; j < 3; j++) {
            k->m[0][j] = 0.3f;
            k->m[1][j] = 0.52f;
            k->m[2][j] = 0.12f;
        }
        k->m[4][0] = 0.12f + 0.08f * t;
        k->m[4][1] = 0.05f * t;
        k->m[4][2] = -0.1f - 0.05f * t;
        k->straight = *D2dFxU(e, 1) == FX_STRAIGHT;
        return TRUE;
    }
    case EFF_EXPOSURE: {
        float g = D2dPowF(2.0f, f[0]);
        k->m[0][0] = k->m[1][1] = k->m[2][2] = g;
        return TRUE;
    }
    case EFF_LUMINANCE:
        k->m[0][0] = k->m[1][1] = k->m[2][2] = k->m[3][3] = 0.0f;
        k->m[0][3] = 0.2125f;
        k->m[1][3] = 0.7154f;
        k->m[2][3] = 0.0721f;
        return TRUE;
    case EFF_WHITELEVEL: {
        float g = f[0] / D2dFxF(e, 1)[0];
        k->m[0][0] = k->m[1][1] = k->m[2][2] = g;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

/* ------------------------------------------------------------------ temperature and tint */

/* robertson's isotemperature lines: reciprocal megakelvin, u, v and slope of the line. */
static const float g_robertson[31][4] = {
    {0.18006f, 0.26352f, -0.24341f, 1e-10f}, {0.18066f, 0.26589f, -0.25479f, 1e-5f}, {0.18133f, 0.26846f, -0.26876f, 2e-5f},
    {0.18208f, 0.27119f, -0.28539f, 3e-5f}, {0.18293f, 0.27407f, -0.30470f, 4e-5f}, {0.18388f, 0.27709f, -0.32675f, 5e-5f},
    {0.18494f, 0.28021f, -0.35156f, 6e-5f}, {0.18611f, 0.28342f, -0.37915f, 7e-5f}, {0.18740f, 0.28668f, -0.40955f, 8e-5f},
    {0.18880f, 0.28997f, -0.44278f, 9e-5f}, {0.19032f, 0.29326f, -0.47888f, 1e-4f}, {0.19462f, 0.30141f, -0.58204f, 1.25e-4f},
    {0.19962f, 0.30921f, -0.70471f, 1.5e-4f}, {0.20525f, 0.31647f, -0.84901f, 1.75e-4f}, {0.21142f, 0.32312f, -1.0182f, 2e-4f},
    {0.21807f, 0.32909f, -1.2168f, 2.25e-4f}, {0.22511f, 0.33439f, -1.4512f, 2.5e-4f}, {0.23247f, 0.33904f, -1.7298f, 2.75e-4f},
    {0.24010f, 0.34308f, -2.0637f, 3e-4f}, {0.24792f, 0.34655f, -2.4681f, 3.25e-4f}, {0.25591f, 0.34951f, -2.9641f, 3.5e-4f},
    {0.26400f, 0.35200f, -3.5814f, 3.75e-4f}, {0.27218f, 0.35407f, -4.3633f, 4e-4f}, {0.28039f, 0.35577f, -5.3762f, 4.25e-4f},
    {0.28863f, 0.35714f, -6.7262f, 4.5e-4f}, {0.29685f, 0.35823f, -8.5955f, 4.75e-4f}, {0.30505f, 0.35907f, -11.324f, 5e-4f},
    {0.31320f, 0.35968f, -15.628f, 5.25e-4f}, {0.32129f, 0.36011f, -23.325f, 5.5e-4f}, {0.32931f, 0.36038f, -40.770f, 5.75e-4f},
    {0.33724f, 0.36051f, -116.45f, 6e-4f},
};

static void temp_tint_xyz(float temp, float tint, float *xyz)
{
    float r = 1.0f / D2dMaxF(1666.6666f, temp);
    tint = D2dClampF(tint, -0.1f, 0.1f);
    int i = 1;
    for (; i < 31; i++) {
        if (r >= g_robertson[i - 1][3] && g_robertson[i][3] >= r)
            break;
    }
    if (i >= 31)
        i = 30;
    const float *a = g_robertson[i - 1], *b = g_robertson[i];
    float t = b[3] == a[3] ? 0.5f : (r - a[3]) / (b[3] - a[3]);
    float la = 1.0f / D2dSqrtF(a[2] * a[2] + 1.0f), lb = 1.0f / D2dSqrtF(b[2] * b[2] + 1.0f);
    float slope = ((b[2] * lb - a[2] * la) * t + a[2] * la) / ((lb - la) * t + la);
    float v0 = (b[1] - a[1]) * t + a[1], u0 = (b[0] - a[0]) * t + a[0];
    float d = -tint / D2dSqrtF(slope * slope + 1.0f);
    float u = d + u0, v = d * slope + v0;
    /* keeps the point on the side of the planckian locus it came from. */
    float lim = 0.4f - u * 0.1f;
    if (v > lim) {
        float k = (lim - v) / ((v0 - (u - u0) * 0.1f) - v);
        u = (u0 - u) * k + u;
        v = (v0 - v) * k + v;
    }
    xyz[1] = 1.0f;
    if (v >= 1e-7f) {
        xyz[0] = u * 6.0f / (v * 4.0f);
        xyz[2] = ((4.0f - u) * 6.0f - v * 60.0f) / (v * 12.0f);
    } else {
        xyz[0] = xyz[2] = 0.0f;
    }
}

static void temp_tint_gains(float temperature, float tint, float *gr, float *gb)
{
    float v = temperature * 100.0f, t1 = 6502.0781f, t2 = 6502.0781f;
    if (v > 0.0f) {
        t2 = 1.0f / (v * 0.0125f * 6.8425245e-5f + 1.5379698e-4f);
    } else if (v < 0.0f) {
        float w = v * 0.0125f;
        t2 = 1.0f / (1.5379698e-4f - w * -5.3796983e-5f);
        t1 = 1.0f / (1.5379698e-4f - w * 8.4298255e-5f);
    }
    float a[3], b[3];
    temp_tint_xyz(t1, 0.003256f, a);
    temp_tint_xyz(t2, tint * 1.25f * 0.01f + 0.003256f, b);
    float ga = a[1] * 1.8760108f - a[0] * 0.969266f + a[2] * 0.041556f;
    float gbb = b[1] * 1.8760108f - b[0] * 0.969266f + b[2] * 0.041556f;
    *gr = ((b[0] * 3.2404542f - b[1] * 1.5371385f - b[2] * 0.4985314f) / gbb) / ((a[0] * 3.2404542f - a[1] * 1.5371385f - a[2] * 0.4985314f) / ga);
    *gb = ((b[0] * 0.0556434f - b[1] * 0.2040259f + b[2] * 1.0572252f) / gbb) / ((a[0] * 0.0556434f - a[1] * 0.2040259f + a[2] * 1.0572252f) / ga);
}

/* ------------------------------------------------------------------ transfers */

typedef struct Transfer {
    int kind;
    BOOL enabled[4];
    BOOL clamp;
    const float *table[4];
    UINT32 count[4];
    float a[4], b[4], c[4];
} Transfer;

enum { TR_TABLE, TR_DISCRETE, TR_LINEAR, TR_GAMMA };

static float transfer_one(const Transfer *t, int ch, float v)
{
    switch (t->kind) {
    case TR_TABLE: {
        UINT32 n = t->count[ch];
        float x = sat(v) * (float)(n - 1);
        UINT32 i = (UINT32)x;
        if (i >= n - 1)
            return t->table[ch][n - 1];
        float f = x - (float)i;
        return t->table[ch][i] + (t->table[ch][i + 1] - t->table[ch][i]) * f;
    }
    case TR_DISCRETE: {
        UINT32 n = t->count[ch];
        float x = sat(v) * (float)n;
        UINT32 i = (UINT32)x;
        return t->table[ch][i >= n ? n - 1 : i];
    }
    case TR_LINEAR:
        return v * t->b[ch] + t->a[ch];
    default: {
        /* amplitude, exponent and offset: an exponent of zero gives one even at zero. */
        float p = t->b[ch] == 0.0f ? 1.0f : D2dPowF(D2dAbsF(v), t->b[ch]);
        return t->a[ch] * p + t->c[ch];
    }
    }
}

static void transfer_apply(const Transfer *t, float *p)
{
    float c[4];
    unpremul(p, c);
    for (int ch = 0; ch < 4; ch++) {
        if (t->enabled[ch])
            c[ch] = transfer_one(t, ch, c[ch]);
        if (t->clamp)
            c[ch] = sat(c[ch]);
    }
    premul_out(c, p);
}

static void transfer_setup(D2dEffect *e, Transfer *t, float *posterize)
{
    memset(t, 0, sizeof(*t));
    switch (e->id) {
    case EFF_TABLETRANSFER:
    case EFF_DISCRETETRANSFER:
        t->kind = e->id == EFF_TABLETRANSFER ? TR_TABLE : TR_DISCRETE;
        for (int ch = 0; ch < 4; ch++) {
            const EffBlob *b = D2dFxB(e, ch * 2);
            t->table[ch] = (const float *)b->data;
            t->count[ch] = b->size / 4;
            t->enabled[ch] = !*D2dFxU(e, ch * 2 + 1) && t->count[ch];
        }
        t->clamp = *D2dFxU(e, 8) != 0;
        break;
    case EFF_LINEARTRANSFER:
        t->kind = TR_LINEAR;
        for (int ch = 0; ch < 4; ch++) {
            t->a[ch] = D2dFxF(e, ch * 3)[0];
            t->b[ch] = D2dFxF(e, ch * 3 + 1)[0];
            t->enabled[ch] = !*D2dFxU(e, ch * 3 + 2);
        }
        t->clamp = *D2dFxU(e, 12) != 0;
        break;
    case EFF_GAMMATRANSFER:
        t->kind = TR_GAMMA;
        for (int ch = 0; ch < 4; ch++) {
            t->a[ch] = D2dFxF(e, ch * 4)[0];
            t->b[ch] = D2dFxF(e, ch * 4 + 1)[0];
            t->c[ch] = D2dFxF(e, ch * 4 + 2)[0];
            t->enabled[ch] = !*D2dFxU(e, ch * 4 + 3);
        }
        t->clamp = *D2dFxU(e, 16) != 0;
        break;
    case EFF_POSTERIZE:
        /* a discrete transfer with evenly spread levels, alpha left alone. */
        t->kind = TR_DISCRETE;
        for (int ch = 0; ch < 3; ch++) {
            UINT32 n = *D2dFxU(e, ch);
            for (UINT32 i = 0; i < n; i++)
                posterize[ch * 16 + i] = (float)(int)i * (1.0f / (float)(int)(n - 1));
            t->table[ch] = posterize + ch * 16;
            t->count[ch] = n;
            t->enabled[ch] = TRUE;
        }
        break;
    default:
        break;
    }
}

/* the exposure curve of the brightness effect, a table transfer of 256 entries on the color channels. */
static float bright_log_to_linear(float v)
{
    return v >= -4.5238094f ? (v * 42.0f * v + v * 380.0f + 859.5238f) / 255.0f : 0.0f;
}

static float bright_map(float shift, float x)
{
    float base = D2dSqrtF(6.0714288f) - 4.5238094f;
    float knee = bright_log_to_linear(base - shift);
    if (knee == 0.0f)
        shift = base - (D2dSqrtF(0.0f) - 4.5238094f);
    float k = knee * 0.37f;
    if (x < k)
        return bright_log_to_linear(D2dSqrtF(k * 255.0f / 42.0f) - 4.5238094f + shift) * x / k;
    return bright_log_to_linear(D2dSqrtF(x * 255.0f / 42.0f) - 4.5238094f + shift);
}

static void brightness_table(D2dEffect *e, float *table)
{
    const float *white = D2dFxF(e, 0), *black = D2dFxF(e, 1);
    float shift = (D2dSqrtF(white[1] * 255.0f / 42.0f) - 4.5238094f) - (D2dSqrtF(white[0] * 255.0f / 42.0f) - 4.5238094f);
    for (int i = 0; i < 256; i++)
        table[i] = bright_map(shift, (float)i / 255.0f);
    float d = bright_map(shift, black[0]) - white[1];
    if (D2dAbsF(d) > 1.1920929e-7f) {
        float k = (black[1] - white[1]) / d;
        for (int i = 0; i < 256; i++)
            table[i] = (table[i] - white[1]) * k + white[1];
    }
}

/* ------------------------------------------------------------------ contrast */

static void contrast_apply(const float *k, BOOL clamp, float *p)
{
    float q[4] = {p[0], p[1], p[2], p[3]}, c[4];
    if (clamp) {
        for (int i = 0; i < 4; i++)
            q[i] = sat(q[i]);
    }
    unpremul(q, c);
    for (int i = 0; i < 3; i++) {
        float x = c[i];
        c[i] = x < 0.5f ? x * (x * k[0] + k[1]) : x * (x * k[2] + k[3]) + k[4];
    }
    premul_out(c, p);
}

/* ------------------------------------------------------------------ hue */

static void rgb_to_hue(BOOL hsl, float *p)
{
    float c[4];
    unpremul(p, c);
    float a = sat(c[3]);
    for (int i = 0; i < 3; i++)
        c[i] = sat(c[i]);
    float lo = D2dMinF(D2dMinF(c[0], c[1]), c[2]), hi = D2dMaxF(D2dMaxF(c[0], c[1]), c[2]), d = hi - lo;
    float h;
    if (d == 0.0f) {
        h = 1.0f;
    } else if (c[0] == hi) {
        h = (c[1] - c[2]) / d;
    } else if (c[1] == hi) {
        h = (c[2] - c[0]) / d + 2.0f;
    } else {
        h = (c[0] - c[1]) / d + 4.0f;
    }
    h = fracf(h * 0.16666667f);
    float o[3];
    o[0] = h;
    if (!hsl) {
        o[1] = hi == 0.0f ? 1.0f : d / hi;
        o[2] = hi;
    } else {
        float sum = lo + hi, den = sum > 1.0f ? 2.0f - sum : sum;
        o[1] = den == 0.0f ? 1.0f : d / den;
        o[2] = sum * 0.5f;
    }
    p[0] = o[0] * a;
    p[1] = o[1] * a;
    p[2] = o[2] * a;
    p[3] = a;
}

static void hue_to_rgb(BOOL hsl, float *p)
{
    float c[4];
    unpremul(p, c);
    float h = c[0], s = c[1], v = c[2];
    float w[3];
    for (int i = 0; i < 3; i++) {
        float t = fracf(h - (float)i / 3.0f) - 0.5f;
        w[i] = sat(D2dAbsF(t) * 6.0f - 1.0f);
    }
    float o[4];
    if (!hsl) {
        for (int i = 0; i < 3; i++)
            o[i] = v * s * w[i] + (v - s * v);
    } else {
        float chroma = s * (1.0f - D2dAbsF(v * 2.0f - 1.0f)), l = v - chroma * 0.5f;
        for (int i = 0; i < 3; i++)
            o[i] = chroma * w[i] + l;
    }
    o[3] = c[3];
    premul_out(o, p);
}

/* ------------------------------------------------------------------ hdr tone map */

typedef struct ToneMap {
    float rgb_to_lms[3][3], lms_to_rgb[3][3];
    float in_max, out_max, knee_lo, knee, mid, top;
    float p;
} ToneMap;

static void mat3_mul(double o[3][3], const double a[3][3], const double b[3][3])
{
    double t[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    memcpy(o, t, sizeof(t));
}

static void mat3_inv(double o[3][3], const double m[3][3])
{
    double d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    double t[3][3] = {{(m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / d, (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d},
                      {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) / d, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d, (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / d},
                      {(m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / d, (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d}};
    memcpy(o, t, sizeof(t));
}

/* srgb primaries with a d65 white of luminance y, as a matrix from rgb to xyz. */
static void rgb_to_xyz(double y, double o[3][3])
{
    static const double p[3][3] = {{0.64 / 0.33, 0.30 / 0.60, 0.15 / 0.06}, {1, 1, 1}, {0.03 / 0.33, 0.10 / 0.60, 0.79 / 0.06}};
    double inv[3][3], w[3] = {0.95045596 * y, y, 1.0890577 * y}, s[3];
    mat3_inv(inv, p);
    for (int i = 0; i < 3; i++)
        s[i] = inv[i][0] * w[0] + inv[i][1] * w[1] + inv[i][2] * w[2];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            o[i][j] = p[i][j] * s[j];
}

static const double g_hpe[3][3] = {{0.4002, 0.7075, -0.0807}, {-0.228, 1.15, 0.0612}, {0, 0, 0.9184}};
static const float g_lms_to_ipt[3][3] = {{0.4f, 0.4f, 0.2f}, {4.455f, -4.851f, 0.396f}, {0.8056f, 0.3572f, -1.1628f}};

static float ipt_f(float v, float p)
{
    float k = D2dPowF(2.0f, p);
    if (v < 0.0f) {
        float w = -v;
        return 0.02f - D2dPowF(w, p) * 246.0f / (D2dPowF(w, p) + k);
    }
    return D2dPowF(v, p) * 246.0f / (D2dPowF(v, p) + k) + 0.02f;
}

static float ipt_exponent(float in_max) { return 0.58f / ((D2dLogF(318.0f) / D2dLogF(in_max)) * (1.25f - 20.0f / in_max / 0.184f * 0.25f)); }

static float lum_to_ipt(float l, float in_max)
{
    float xyz[3] = {l * 0.95042855f / in_max, l / in_max, l * 1.0889004f / in_max}, lms[3];
    float p = ipt_exponent(in_max);
    for (int i = 0; i < 3; i++)
        lms[i] = ipt_f((float)(g_hpe[i][0] * xyz[0] + g_hpe[i][1] * xyz[1] + g_hpe[i][2] * xyz[2]), p);
    return g_lms_to_ipt[0][0] * lms[0] + g_lms_to_ipt[0][1] * lms[1] + g_lms_to_ipt[0][2] * lms[2];
}

static void tonemap_setup(D2dEffect *e, ToneMap *t)
{
    float in = D2dFxF(e, 0)[0], out = D2dFxF(e, 1)[0], prop_out = out;
    BOOL hdr = *D2dFxU(e, 2) != 0;
    if (!hdr && in > 160.0f) {
        out = D2dMaxF(150.0f, out * 0.75f);
        in = D2dMaxF(4000.0f, in);
    }
    t->in_max = in;
    t->out_max = out;
    t->knee_lo = lum_to_ipt(out - out * 0.4f, in) - 0.05f;
    t->knee = hdr ? 0.0f : lum_to_ipt(out * 0.1f, in);
    t->mid = lum_to_ipt(in, in);
    t->top = lum_to_ipt(out, in);
    t->p = ipt_exponent(in);
    double m[3][3], x[3][3], inv[3][3];
    rgb_to_xyz(80.0 / in, x);
    mat3_mul(m, g_hpe, x);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            t->rgb_to_lms[i][j] = (float)m[i][j];
    rgb_to_xyz((out / prop_out) * 80.0 / in, x);
    mat3_inv(x, x);
    mat3_inv(inv, g_hpe);
    mat3_mul(m, x, inv);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            t->lms_to_rgb[i][j] = (float)m[i][j];
}

static void tonemap_apply(const ToneMap *t, float *px)
{
    float c[4];
    unpremul(px, c);
    float lms[3], ipt[3];
    float k = D2dPowF(2.0f, t->p);
    for (int i = 0; i < 3; i++) {
        float v = t->rgb_to_lms[i][0] * c[0] + t->rgb_to_lms[i][1] * c[1] + t->rgb_to_lms[i][2] * c[2];
        float sign = v > 0.0f ? 246.0f : (v < 0.0f ? -246.0f : 0.0f);
        float q = D2dPowF(D2dAbsF(v), t->p);
        lms[i] = sign * q / (k + q) + 0.02f;
    }
    for (int i = 0; i < 3; i++)
        ipt[i] = g_lms_to_ipt[i][0] * lms[0] + g_lms_to_ipt[i][1] * lms[1] + g_lms_to_ipt[i][2] * lms[2];
    float I = ipt[0], n;
    if (t->knee >= I) {
        float s = D2dMaxF(I / t->knee, 0.0f), w = 1.0f - s;
        float den = s * s + 0.35f * w * w + 2.0f * w * s;
        float num = (t->knee + 0.05f) * s * s + 2.0f * w * s * (t->knee * 0.5f);
        n = num / den;
    } else if (t->knee_lo >= I) {
        n = I + 0.05f;
    } else if (I < t->mid) {
        float kk = t->in_max / t->out_max * 0.4f;
        float s = (I - t->knee_lo) / (t->mid - t->knee_lo + 1e-5f), w = 1.0f - s;
        float a0 = t->knee_lo + 0.05f, w0 = t->top / a0 * w * w;
        float den = 2.0f * w * s * kk + w0 + s * s * kk;
        float num = w0 * a0 + 2.0f * w * s * t->top * kk + kk * s * s * t->top;
        n = num / den;
    } else {
        n = t->top;
    }
    float ratio = D2dPowF(D2dMinF(D2dAbsF(n / I), 1.0f), 1.2f);
    float q[3] = {n, ipt[1] * ratio, ipt[2] * ratio};
    static const float back[3][3] = {{1.0f, 0.0975689292f, 0.205226436f}, {1.0f, -0.113876484f, 0.133217156f}, {1.0f, 0.0326151103f, -0.676887155f}};
    float o[3];
    for (int i = 0; i < 3; i++) {
        float y = back[i][0] * q[0] + back[i][1] * q[1] + back[i][2] * q[2];
        float sign = y > 0.0f ? 1.0f : (y < 0.0f ? -1.0f : 0.0f);
        float r = (y * 50.0f - 1.0f) * k / (y * 50.0f - 12301.0f);
        lms[i] = D2dPowF(D2dAbsF(r), 1.0f / t->p) * sign;
    }
    for (int i = 0; i < 3; i++)
        o[i] = t->lms_to_rgb[i][0] * lms[0] + t->lms_to_rgb[i][1] * lms[1] + t->lms_to_rgb[i][2] * lms[2];
    px[0] = o[0] * c[3];
    px[1] = o[1] * c[3];
    px[2] = o[2] * c[3];
}

/* ------------------------------------------------------------------ 3d lookup tables */

static void lut_sample(const D2dLut3D *l, const float *coord, float *out)
{
    /* coord is blue, green, red: texel centers span the unit cube. */
    float f[3];
    int i0[3], i1[3];
    for (int k = 0; k < 3; k++) {
        float x = sat(coord[k]) * (float)(l->n[k] - 1);
        int i = (int)x;
        if (i >= (int)l->n[k] - 1)
            i = (int)l->n[k] - 1;
        i0[k] = i;
        i1[k] = i + 1 < (int)l->n[k] ? i + 1 : i;
        f[k] = x - (float)i;
    }
    for (int ch = 0; ch < 4; ch++)
        out[ch] = 0.0f;
    for (int corner = 0; corner < 8; corner++) {
        int ix = corner & 1 ? i1[0] : i0[0], iy = corner & 2 ? i1[1] : i0[1], iz = corner & 4 ? i1[2] : i0[2];
        float w = (corner & 1 ? f[0] : 1.0f - f[0]) * (corner & 2 ? f[1] : 1.0f - f[1]) * (corner & 4 ? f[2] : 1.0f - f[2]);
        if (w == 0.0f)
            continue;
        const float *t = l->data + (((SIZE_T)iz * l->n[1] + iy) * l->n[0] + ix) * 4;
        for (int ch = 0; ch < 4; ch++)
            out[ch] += t[ch] * w;
    }
}

/* ------------------------------------------------------------------ chroma key */

static void chroma_key(const float *key, float tol, BOOL invert, float *p)
{
    float c[4];
    unpremul(p, c);
    float d[3] = {c[0] - key[0], c[1] - key[1], c[2] - key[2]};
    float m = D2dSqrtF(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) < tol ? 1.0f : 0.0f;
    float k = invert ? m : 1.0f - m;
    for (int i = 0; i < 4; i++)
        c[i] *= k;
    premul_out(c, p);
}

/* the alpha of the keyed pixels softened by their neighbours, read as bilinear taps half a pixel apart. */
static float feather_tap(const FxImg *im, float x, float y)
{
    int x0 = (int)D2dFloorF(x), y0 = (int)D2dFloorF(y);
    float fx = x - (float)x0, fy = y - (float)y0, a = 0.0f;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            int xx = x0 + i, yy = y0 + j;
            float w = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
            if (w != 0.0f && xx >= im->x && xx < im->x + im->w && yy >= im->y && yy < im->y + im->h)
                a += fx_px(im, xx, yy)[3] * w;
        }
    }
    return a;
}

/* ------------------------------------------------------------------ bounds and rendering */

HRESULT FxColorBounds(FxCtx *c, FxRect *b) { return FxInputBounds(c, 0, b); }

static HRESULT vignette(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    FxRect in;
    HRESULT hr = FxInputBounds(c, 0, &in);
    if (SUCCEEDED(hr))
        hr = FxInput(c, 0, area, out);
    if (FAILED(hr) || !out->px || fx_infinite(in))
        return hr;
    const float *color = D2dFxF(e, 0);
    float size = D2dFxF(e, 1)[0], strength = D2dFxF(e, 2)[0];
    if (size < 1.1920929e-7f)
        return S_OK;
    float d = D2dMinF((float)(in.x1 - in.x0) * 0.5f * size, (float)(in.y1 - in.y0) * 0.5f * size);
    if (!(d > 0.0f))
        return S_OK;
    float x0 = (float)in.x0 + d, y0 = (float)in.y0 + d, x1 = (float)in.x1 - d, y1 = (float)in.y1 - d;
    float scale = 1.0f / d, offset = (1.0f - strength) * 0.75f;
    for (int y = out->y; y < out->y + out->h; y++) {
        float py = (float)y + 0.5f, dy = py < y0 ? y0 - py : (py > y1 ? py - y1 : 0.0f);
        for (int x = out->x; x < out->x + out->w; x++) {
            float px = (float)x + 0.5f, dx = px < x0 ? x0 - px : (px > x1 ? px - x1 : 0.0f);
            float t = sat(D2dSqrtF(dx * dx + dy * dy) * scale - offset);
            t = t * t * (3.0f - 2.0f * t);
            float *p = fx_px(out, x, y);
            for (int k = 0; k < 4; k++)
                p[k] += (color[k] - p[k]) * t;
        }
    }
    return S_OK;
}

static HRESULT chroma_key_render(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    const float *key = D2dFxF(e, 0);
    float tol = D2dFxF(e, 1)[0];
    BOOL invert = *D2dFxU(e, 2) != 0, feather = *D2dFxU(e, 3) != 0;
    if (!feather) {
        HRESULT hr = FxInput(c, 0, area, out);
        if (SUCCEEDED(hr) && out->px) {
            for (SIZE_T i = 0; i < (SIZE_T)out->w * out->h; i++)
                chroma_key(key, tol, invert, out->px + i * 4);
        }
        return hr;
    }
    FxImg keyed;
    HRESULT hr = FxInput(c, 0, fx_inflate(area, 1, 1), &keyed);
    if (FAILED(hr))
        return hr;
    if (keyed.px) {
        for (SIZE_T i = 0; i < (SIZE_T)keyed.w * keyed.h; i++)
            chroma_key(key, tol, invert, keyed.px + i * 4);
    }
    hr = FxNew(out, area);
    if (SUCCEEDED(hr) && out->px && keyed.px) {
        for (int y = area.y0; y < area.y1; y++) {
            for (int x = area.x0; x < area.x1; x++) {
                float cx = (float)x, cy = (float)y;
                const float *ctr = fx_px(&keyed, x, y);
                float sum = ctr[3] + 2.0f * (feather_tap(&keyed, cx - 0.5f, cy - 1.0f) + feather_tap(&keyed, cx + 1.0f, cy - 0.5f) +
                                             feather_tap(&keyed, cx - 1.0f, cy + 0.5f) + feather_tap(&keyed, cx + 0.5f, cy + 1.0f));
                float a = sat((sum - 5.0f) * 0.25f), s[4];
                unpremul(ctr, s);
                float *o = fx_px(out, x, y);
                o[0] = s[0] * a;
                o[1] = s[1] * a;
                o[2] = s[2] * a;
                o[3] = a;
            }
        }
    }
    FxFree(&keyed);
    return hr;
}

static void histogram_count(D2dEffect *e, const FxImg *im)
{
    UINT32 bins = *D2dFxU(e, 0), ch = *D2dFxU(e, 1);
    if (e->histogram_bins != bins) {
        D2dFree(e->histogram);
        e->histogram = D2dAllocZero((SIZE_T)bins * 4);
        e->histogram_bins = e->histogram ? bins : 0;
    }
    if (!e->histogram)
        return;
    memset(e->histogram, 0, (SIZE_T)bins * 4);
    SIZE_T n = (SIZE_T)im->w * im->h;
    if (!n)
        return;
    for (SIZE_T i = 0; i < n; i++) {
        float c[4];
        unpremul(im->px + i * 4, c);
        float v = D2dFloorF(c[ch] * (float)bins);
        UINT32 b = v > 0.0f ? (UINT32)v : 0;
        e->histogram[b >= bins ? bins - 1 : b] += 1.0f;
    }
    for (UINT32 b = 0; b < bins; b++)
        e->histogram[b] /= (float)n;
}

HRESULT FxColorRender(FxCtx *c, FxRect area, FxImg *out)
{
    D2dEffect *e = c->e;
    if (e->id == EFF_VIGNETTE)
        return vignette(c, area, out);
    if (e->id == EFF_CHROMAKEY)
        return chroma_key_render(c, area, out);
    HRESULT hr = FxInput(c, 0, area, out);
    if (FAILED(hr) || !out->px)
        return hr;
    SIZE_T n = (SIZE_T)out->w * out->h;
    float *px = out->px;
    Matrix54 k;
    if (effect_matrix(e, &k)) {
        for (SIZE_T i = 0; i < n; i++)
            matrix_apply(&k, px + i * 4);
        return S_OK;
    }
    switch (e->id) {
    case EFF_INVERT:
        for (SIZE_T i = 0; i < n; i++) {
            float *p = px + i * 4;
            for (int j = 0; j < 3; j++)
                p[j] = p[3] == 0.0f ? 0.0f : (1.0f - p[j] / p[3]) * p[3];
        }
        break;
    case EFF_PREMULTIPLY:
        for (SIZE_T i = 0; i < n; i++) {
            float *p = px + i * 4;
            p[0] *= p[3];
            p[1] *= p[3];
            p[2] *= p[3];
        }
        break;
    case EFF_UNPREMULTIPLY:
        for (SIZE_T i = 0; i < n; i++) {
            float *p = px + i * 4, q[4];
            unpremul(p, q);
            memcpy(p, q, sizeof(q));
        }
        break;
    case EFF_CONTRAST: {
        float v = D2dFxF(e, 0)[0] * 0.75f, s = 1.0f - v;
        float kk[5] = {(1.0f - s) + (1.0f - s), s, (s - 1.0f) + (s - 1.0f), 4.0f - s * 3.0f, s - 1.0f};
        BOOL clamp = *D2dFxU(e, 1) != 0;
        for (SIZE_T i = 0; i < n; i++)
            contrast_apply(kk, clamp, px + i * 4);
        break;
    }
    case EFF_BRIGHTNESS: {
        float table[256];
        brightness_table(e, table);
        Transfer t = {TR_TABLE, {TRUE, TRUE, TRUE, FALSE}, FALSE, {table, table, table, nullptr}, {256, 256, 256, 0}};
        for (SIZE_T i = 0; i < n; i++)
            transfer_apply(&t, px + i * 4);
        break;
    }
    case EFF_TABLETRANSFER:
    case EFF_DISCRETETRANSFER:
    case EFF_LINEARTRANSFER:
    case EFF_GAMMATRANSFER:
    case EFF_POSTERIZE: {
        Transfer t;
        float levels[48];
        transfer_setup(e, &t, levels);
        for (SIZE_T i = 0; i < n; i++)
            transfer_apply(&t, px + i * 4);
        break;
    }
    case EFF_TEMPERATURETINT: {
        Matrix54 m;
        matrix_identity(&m);
        temp_tint_gains(D2dFxF(e, 0)[0], D2dFxF(e, 1)[0], &m.m[0][0], &m.m[2][2]);
        for (SIZE_T i = 0; i < n; i++)
            matrix_apply(&m, px + i * 4);
        break;
    }
    case EFF_RGBTOHUE:
    case EFF_HUETORGB: {
        BOOL hsl = *D2dFxU(e, 0) != 0;
        for (SIZE_T i = 0; i < n; i++) {
            if (e->id == EFF_RGBTOHUE)
                rgb_to_hue(hsl, px + i * 4);
            else
                hue_to_rgb(hsl, px + i * 4);
        }
        break;
    }
    case EFF_HDRTONEMAP: {
        ToneMap t;
        tonemap_setup(e, &t);
        for (SIZE_T i = 0; i < n; i++)
            tonemap_apply(&t, px + i * 4);
        break;
    }
    case EFF_LOOKUPTABLE3D: {
        D2dLut3D *l = D2dLut3DOf(*(void **)e->props[0]);
        if (!l)
            break;
        BOOL straight = *D2dFxU(e, 1) == FX_STRAIGHT;
        for (SIZE_T i = 0; i < n; i++) {
            float *p = px + i * 4, q[4];
            if (straight)
                memcpy(q, p, sizeof(q));
            else
                unpremul(p, q);
            float coord[3] = {q[2], q[1], q[0]};
            /* a transparent pixel reads the first texel. */
            if (!straight && p[3] == 0.0f)
                coord[0] = coord[1] = coord[2] = 0.0f;
            lut_sample(l, coord, p);
        }
        break;
    }
    case EFF_HISTOGRAM:
        histogram_count(e, out);
        break;
    default:
        break;
    }
    return S_OK;
}
